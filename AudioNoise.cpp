/*
The MIT License (MIT)

Copyright (c) 2025 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <algorithm>
#include <numbers>
#include <numeric>
#include <limits>
#include <complex>
#include <bit>
#include <memory>
#include <tuple>
#include <concepts>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using byte = uint8_t;
#include <exedit.hpp>
#include "philox.hpp"
#include "fft.hpp"


////////////////////////////////
// 主要情報源の変数アドレス．
////////////////////////////////
struct param_dialog_info {
	// script_param_dialog() 関数 (exedit_h + 0x022c80) に配列として渡して，操作対象の exdata 内のフィールドを指定．
	int16_t const_3;	// 配列の末尾に 0 を置く．3 以外の数値だと無意味？
	int16_t idx_use;	// exdata_use 内の index.
	char const* name;	// ダイアログに表示される項目名．
};

constinit struct ExEdit092 {
	void init(AviUtl::FilterPlugin* efp)
	{
		if (fp == nullptr)
			init_pointers(efp);
	}
	AviUtl::FilterPlugin* fp = nullptr;

	void** memory_ptr;	// 0x1a5328 // 少なくとも最大画像サイズと同サイズは保証されるっぽい．

	// called at: exedit_base + 0x1c1ea.
	void* (*get_or_create_cache)(ExEdit::ObjectFilterIndex ofi, int w, int h, int bitcount, int v_func_id, int* old_cache_exists);

	// 0x04a7e0
	void(*update_any_exdata)(ExEdit::ObjectFilterIndex processing, char const* exdata_use_name);

	// 0x022c80
	BOOL(*script_param_dialog)(ExEdit::Filter* efp, param_dialog_info const* info);

private:
	void init_pointers(AviUtl::FilterPlugin* efp)
	{
		fp = efp;

		auto pick_addr = [exedit_base = reinterpret_cast<intptr_t>(efp->dll_hinst)]
			<class T>(T & target, ptrdiff_t offset) { target = reinterpret_cast<T>(exedit_base + offset); };
		auto pick_call_addr = [&]<class T>(T & target, ptrdiff_t offset) {
			ptrdiff_t* tmp; pick_addr(tmp, offset);
			target = reinterpret_cast<T>(4 + reinterpret_cast<intptr_t>(tmp) + *tmp);
		};

		pick_addr(memory_ptr,				0x1a5328);
		pick_call_addr(get_or_create_cache,	0x01c1ea);
		pick_addr(update_any_exdata,		0x04a7e0);
		pick_addr(script_param_dialog,		0x022c80);
	}
} exedit{};


////////////////////////////////
// 仕様書．
////////////////////////////////
struct check_data {
	enum def : int32_t {
		unchecked = 0,
		checked = 1,
		button = -1,
		dropdown = -2,
	};
};

#define PLUGIN_VERSION	"v1.10-test8"
#define PLUGIN_AUTHOR	"sigma-axis"
#define FILTER_INFO_FMT(name, ver, author)	(name " " ver " by " author)
#define FILTER_INFO(name)	constexpr char filter_name[] = name, info[] = FILTER_INFO_FMT(name, PLUGIN_VERSION, PLUGIN_AUTHOR)

namespace noise
{
	FILTER_INFO("音声ノイズ");

	// trackbars.
	constexpr char const* track_names[] = { "指数", "分解能", "背景音量" };
	constexpr int32_t
		track_denom[]	= {    100,   100,   10 },
		track_min[]		= { -40000, -4800,    0 },
		track_min_drag[]= { -20000,     0,    0 },
		track_default[]	= {      0, +9600, 1000 },
		track_max_drag[]= { +20000, +7200, 1000 },
		track_max[]		= { +40000, +9600, 2000 };

	static_assert(
		std::size(track_names) == std::size(track_denom) &&
		std::size(track_names) == std::size(track_min) &&
		std::size(track_names) == std::size(track_min_drag) &&
		std::size(track_names) == std::size(track_default) &&
		std::size(track_names) == std::size(track_max_drag) &&
		std::size(track_names) == std::size(track_max));

	namespace idx_track
	{
		enum id : int {
			alpha,
			resolution,
			back_volume,
		};
	};

	// checks.
	constexpr char const* check_names[] = {
		"ステレオ",
		"補間する",
		"設定...",
	};
	constexpr int32_t check_default[] = {
		check_data::unchecked,
		check_data::unchecked,
		check_data::button,
	};

	static_assert(std::size(check_names) == std::size(check_default));

	namespace idx_check
	{
		enum id : int {
			stereo,
			interpolate,
			detail,
		};
	};

	// exdata.
	struct Exdata {
		int32_t seed;
		uint32_t fft_size; // bit-ceiling value is used.

		constexpr static decltype(fft_size)
			min_fft_size = 1u << 9, max_fft_size = 1u << 13;
		static constexpr auto clamp(decltype(fft_size) fft_size) {
			return std::clamp(std::bit_ceil(fft_size), min_fft_size, max_fft_size);
		}
		constexpr auto clamped_fft_size() const { return clamp(fft_size); }
	};
	constexpr Exdata exdata_def = { 0, 2048 };
	constexpr ExEdit::ExdataUse exdata_use[] =
	{
		{.type = ExEdit::ExdataUse::Type::Number, .size = 4, .name = "seed" },
		{.type = ExEdit::ExdataUse::Type::Number, .size = 4, .name = "fft_size" },
	};

	static_assert(sizeof(Exdata) == std::accumulate(
		std::begin(exdata_use), std::end(exdata_use), size_t{ 0 }, [](auto v, auto d) { return v + d.size; }));

	namespace idx_exdata
	{
		enum id : int {
			seed,
			fft_size,
		};
	};

	// callbacks.
	BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip);
	template<class enum_check>
	BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, ExEdit::Filter* efp);
	template<class enum_check>
	int32_t func_window_init(HINSTANCE hinstance, HWND hwnd, int y, int base_id, int sw_param, ExEdit::Filter* efp);

	// spec.
	inline constinit ExEdit::Filter filter = {
		.flag				= ExEdit::Filter::Flag::Audio | ExEdit::Filter::Flag::Input,
		.name				= const_cast<char*>(filter_name),
		.track_n			= std::size(track_names),
		.track_name			= const_cast<char**>(track_names),
		.track_default		= const_cast<int*>(track_default),
		.track_s			= const_cast<int*>(track_min),
		.track_e			= const_cast<int*>(track_max),
		.check_n			= std::size(check_names),
		.check_name			= const_cast<char**>(check_names),
		.check_default		= const_cast<int*>(check_default),
		.func_proc			= &func_proc,
		.func_init			= [](ExEdit::Filter* efp) { exedit.init(efp->exedit_fp); return TRUE; },
		.func_WndProc		= &func_WndProc<idx_check::id>,
		.exdata_size		= sizeof(Exdata),
		.information		= const_cast<char*>(info),
		.func_window_init	= &func_window_init<idx_check::id>,
		.exdata_def			= const_cast<Exdata*>(&exdata_def),
		.exdata_use			= exdata_use,
		.track_scale		= const_cast<int*>(track_denom),
		.track_drag_min		= const_cast<int*>(track_min_drag),
		.track_drag_max		= const_cast<int*>(track_max_drag),
	};

	// constants.
	template<int std_height>
	inline/*constexpr*/ int16_t to_int_t(std::floating_point auto x) {
		using lim = std::numeric_limits<int16_t>;
		return static_cast<int16_t>(std::lround(std::clamp<decltype(x)>(
			std_height * x, lim::min(), lim::max())));
	}
	constexpr int std_height = -(std::numeric_limits<int16_t>::min() >> 3);
	constexpr auto to_int(auto x) { return to_int_t<std_height>(x); }
}

namespace noise_multiply
{
	FILTER_INFO("音声ノイズ乗算");

	// trackbars.
	constexpr char const* track_names[] = { "強さ", "指数", "分解能", "頭打ちdB", "足切りdB" };
	constexpr int32_t
		track_denom[]	= {   10,    100,   100,   100,   100 },
		track_min[]		= {    0, -40000, -4800, -7200, -7200 },
		track_min_drag[]= {    0, -20000,     0, -6600, -6600 },
		track_default[]	= { 1000,      0, +9600,     0, -7200 },
		track_max_drag[]= { 1000, +20000, +7200, +1200, +1200 },
		track_max[]		= { 1000, +40000, +9600, +2400, +2400 };

	static_assert(
		std::size(track_names) == std::size(track_denom) &&
		std::size(track_names) == std::size(track_min) &&
		std::size(track_names) == std::size(track_min_drag) &&
		std::size(track_names) == std::size(track_default) &&
		std::size(track_names) == std::size(track_max_drag) &&
		std::size(track_names) == std::size(track_max));

	namespace idx_track
	{
		enum id : int {
			intensity,
			alpha,
			resolution,
			up_bound,
			lo_bound,
		};
	};

	// checks.
	constexpr char const* check_names[] = {
		"ステレオ",
		"補間する",
		"ON/OFF反転",
		"設定...",
	};
	constexpr int32_t check_default[] = {
		check_data::unchecked,
		check_data::unchecked,
		check_data::unchecked,
		check_data::button,
	};

	static_assert(std::size(check_names) == std::size(check_default));

	namespace idx_check
	{
		enum id : int {
			stereo,
			interpolate,
			invert,
			detail,
		};
	};

	// exdata.
	using noise::Exdata, noise::exdata_def, noise::exdata_use;
	namespace idx_exdata = noise::idx_exdata;

	// callbacks.
	BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip);
	using noise::func_WndProc, noise::func_window_init;

	// spec.
	consteval ExEdit::Filter make_filter(ExEdit::Filter::Flag flag) {
		return {
			.flag				= ExEdit::Filter::Flag::Audio | flag,
			.name				= const_cast<char*>(filter_name),
			.track_n			= std::size(track_names),
			.track_name			= const_cast<char**>(track_names),
			.track_default		= const_cast<int*>(track_default),
			.track_s			= const_cast<int*>(track_min),
			.track_e			= const_cast<int*>(track_max),
			.check_n			= std::size(check_names),
			.check_name			= const_cast<char**>(check_names),
			.check_default		= const_cast<int*>(check_default),
			.func_proc			= &func_proc,
			.func_WndProc		= &func_WndProc<idx_check::id>,
			.exdata_size		= sizeof(Exdata),
			.information		= const_cast<char*>(info),
			.func_window_init	= &func_window_init<idx_check::id>,
			.exdata_def			= const_cast<Exdata*>(&exdata_def),
			.exdata_use			= exdata_use,
			.track_scale		= const_cast<int*>(track_denom),
			.track_drag_min		= const_cast<int*>(track_min_drag),
			.track_drag_max		= const_cast<int*>(track_max_drag),
		};
	};
	inline constinit ExEdit::Filter
		effect = make_filter(ExEdit::Filter::Flag::Effect),
		filter = make_filter({});
}

namespace velvet
{
	FILTER_INFO("Velvet Noise");

	// trackbars.
	constexpr char const* track_names[] = { "密度", "指数", "分解能", "背景音量" };
	constexpr int32_t
		track_denom[]	= {   100,    100,   100,   10 },
		track_min[]		= { -4800, -40000, -4800,    0 },
		track_min_drag[]= {     0, -20000,     0,    0 },
		track_default[]	= { +3300,      0, +9600, 1000 },
		track_max_drag[]= { +7200, +20000, +7200, 1000 },
		track_max[]		= { +9600, +40000, +9600, 2000 };

	static_assert(
		std::size(track_names) == std::size(track_denom) &&
		std::size(track_names) == std::size(track_min) &&
		std::size(track_names) == std::size(track_min_drag) &&
		std::size(track_names) == std::size(track_default) &&
		std::size(track_names) == std::size(track_max_drag) &&
		std::size(track_names) == std::size(track_max));

	namespace idx_track
	{
		enum id : int {
			fuzzy,
			alpha,
			resolution,
			back_volume,
		};
	};

	// checks.
	using noise::check_names, noise::check_default;
	namespace idx_check = noise::idx_check;

	// exdata.
	using noise::Exdata, noise::exdata_def, noise::exdata_use;
	namespace idx_exdata = noise::idx_exdata;

	// callbacks.
	BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip);
	using noise::func_WndProc, noise::func_window_init;

	// spec.
	inline constinit ExEdit::Filter filter = {
		.flag				= ExEdit::Filter::Flag::Audio | ExEdit::Filter::Flag::Input,
		.name				= const_cast<char*>(filter_name),
		.track_n			= std::size(track_names),
		.track_name			= const_cast<char**>(track_names),
		.track_default		= const_cast<int*>(track_default),
		.track_s			= const_cast<int*>(track_min),
		.track_e			= const_cast<int*>(track_max),
		.check_n			= std::size(check_names),
		.check_name			= const_cast<char**>(check_names),
		.check_default		= const_cast<int*>(check_default),
		.func_proc			= &func_proc,
		.func_init			= [](ExEdit::Filter* efp) { exedit.init(efp->exedit_fp); return TRUE; },
		.func_WndProc		= &func_WndProc<idx_check::id>,
		.exdata_size		= sizeof(Exdata),
		.information		= const_cast<char*>(info),
		.func_window_init	= &func_window_init<idx_check::id>,
		.exdata_def			= const_cast<Exdata*>(&exdata_def),
		.exdata_use			= exdata_use,
		.track_scale		= const_cast<int*>(track_denom),
		.track_drag_min		= const_cast<int*>(track_min_drag),
		.track_drag_max		= const_cast<int*>(track_max_drag),
	};

	// constants.
	using noise::to_int_t;
	constexpr int std_height = noise::std_height << 2;
	constexpr auto to_int(auto x) { return to_int_t<std_height>(x); }
}

namespace pulse
{
	FILTER_INFO("パルスノイズ");

	// trackbars.
	constexpr char const* track_names[] = { "位置(ms)", "幅(ms)", "背景音量" };
	constexpr int32_t
		track_denom[]	= {   100,   100,   10 },
		track_min[]		= {     0,     0,    0 },
		track_min_drag[]= {     0,     0,    0 },
		track_default[]	= {     0,     0, 1000 },
		track_max_drag[]= {  4000,   200, 1000 },
		track_max[]		= { 50000, 20000, 2000 };

	static_assert(
		std::size(track_names) == std::size(track_denom) &&
		std::size(track_names) == std::size(track_min) &&
		std::size(track_names) == std::size(track_min_drag) &&
		std::size(track_names) == std::size(track_default) &&
		std::size(track_names) == std::size(track_max_drag) &&
		std::size(track_names) == std::size(track_max));

	namespace idx_track
	{
		enum id : int {
			position,
			duration,
			back_volume,
		};
	};

	// callback.
	BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip);

	// spec.
	inline constinit ExEdit::Filter filter = {
		.flag			= ExEdit::Filter::Flag::Audio | ExEdit::Filter::Flag::Input,
		.name			= const_cast<char*>(filter_name),
		.track_n		= std::size(track_names),
		.track_name		= const_cast<char**>(track_names),
		.track_default	= const_cast<int*>(track_default),
		.track_s		= const_cast<int*>(track_min),
		.track_e		= const_cast<int*>(track_max),
		.func_proc		= &func_proc,
		.information	= const_cast<char*>(info),
		.track_scale	= const_cast<int*>(track_denom),
		.track_drag_min	= const_cast<int*>(track_min_drag),
		.track_drag_max	= const_cast<int*>(track_max_drag),
	};

	// constants.
	constexpr int pulse_height = 1 << 14;
}


////////////////////////////////
// ウィンドウ状態の保守．
////////////////////////////////
static inline void update_window_state(int idx_detail, ExEdit::Filter* efp)
{
	/*
	efp->exfunc->get_hwnd(efp->processing, i, j):
		i = 0:		j 番目のスライダーの中央ボタン．
		i = 1:		j 番目のスライダーの左トラックバー．
		i = 2:		j 番目のスライダーの右トラックバー．
		i = 3:		j 番目のチェック枠のチェックボックス．
		i = 4:		j 番目のチェック枠のボタン．
		i = 5, 7:	j 番目のチェック枠の右にある static (テキスト).
		i = 6:		j 番目のチェック枠のコンボボックス．
		otherwise -> nullptr.
	*/

	auto const* exdata = reinterpret_cast<noise::Exdata*>(efp->exdata_ptr);

	// ボタン横のテキスト設定.
	wchar_t text[std::bit_ceil(std::size(L"シード: -2147483648 / FFTサイズ: 8192****"))];
	::swprintf_s(text, L"シード: %d / FFTサイズ: %d", exdata->seed, exdata->clamped_fft_size());
	::SetWindowTextW(efp->exfunc->get_hwnd(efp->processing, 5, idx_detail), text);
}

static inline BOOL common_func_WndProc(int idx_detail, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, ExEdit::Filter* efp)
{
	if (message != ExEdit::ExtendedFilter::Message::WM_EXTENDEDFILTER_COMMAND) return FALSE;
	using noise::Exdata, noise::exdata_use;
	namespace idx_exdata = noise::idx_exdata;

	auto* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);
	auto chk = static_cast<int32_t>(wparam >> 16);
	auto cmd = wparam & 0xffff;

	switch (cmd) {
		using namespace ExEdit::ExtendedFilter::CommandId;
	case EXTENDEDFILTER_PUSH_BUTTON:
		if (chk == idx_detail) {
			// 詳細設定のボタン．
			constexpr param_dialog_info info[] = {
				{.const_3 = 3, .idx_use = idx_exdata::seed, .name = "シード" },
				{.const_3 = 3, .idx_use = idx_exdata::fft_size, .name = "FFTサイズ" },

				{.const_3 = 0, .idx_use = 0, .name = nullptr },
			};

			// バックアップを取って項目操作のダイアログを表示．
			auto prev = *exdata;
			exedit.script_param_dialog(efp, info); // this function always returns TRUE.

			// adjust fft_size into the acceptable range.
			exdata->fft_size = Exdata::clamp(exdata->fft_size);

			// 相違点があるなら「元に戻す」にデータ記録．
			if (std::memcmp(&prev, exdata, sizeof(prev)) != 0) {
				{
					auto next = *exdata;
					*exdata = prev;
					efp->exfunc->set_undo(efp->processing, 0);
					*exdata = next;
				}

				if (prev.seed != exdata->seed)
					exedit.update_any_exdata(efp->processing, exdata_use[idx_exdata::seed].name);
				if (prev.fft_size != exdata->fft_size)
					exedit.update_any_exdata(efp->processing, exdata_use[idx_exdata::fft_size].name);

				update_window_state(idx_detail, efp);
				return TRUE;
			}
		}
		break;
	}
	return FALSE;
}

template<class enum_check>
BOOL noise::func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, ExEdit::Filter* efp)
{
	return common_func_WndProc(enum_check::detail, hwnd, message, wparam, lparam, editp, efp);
}

template<class enum_check>
int32_t noise::func_window_init(HINSTANCE hinstance, HWND hwnd, int y, int base_id, int sw_param, ExEdit::Filter* efp)
{
	if (sw_param != 0) update_window_state(enum_check::detail, efp);
	return 0;
}


////////////////////////////////
// 正規分布の乱数器．
////////////////////////////////
template<std::floating_point base_float>
struct normal_rng {
	constexpr normal_rng(uint32_t seed)
		: core{ seed ^ philox::default_seed }
		, r{ nan } {}

	// returns a random number according to the normal distribution.
	// std dev is 1 and mean is 0.
	constexpr base_float operator()() {
		if (!std::isnan(r)) return std::exchange(r, nan);

		auto a = core() / N, b = core() / N;
		a *= 2 * pi;
		b = std::sqrt(-2 * std::log(1 - b));
		r = static_cast<base_float>(b * std::sin(a));
		return static_cast<base_float>(b * std::cos(a));
	}
	constexpr void discard(uint_fast64_t n) {
		if (n == 0) return;
		if (!std::isnan(r)) { r = nan; n--; }
		core.discard(n & (~1uLL));
		if ((n & 1u) != 0) (*this)();
	}

private:
	using philox = sigma_lib::rng::philox_test::philox4x32;
	philox core;
	base_float r;
	constexpr static base_float
		nan = std::numeric_limits<base_float>::quiet_NaN();
	constexpr static double
		pi	= std::numbers::pi_v<double>,
		N	= static_cast<double>(philox::max()) + 1;
	static_assert(
		std::numeric_limits<decltype(N)>::digits >=
		std::numeric_limits<philox::result_type>::digits); // log(1 - b) could be $-\infty$ otherwise.
};


////////////////////////////////
// ノイズ生成．
////////////////////////////////
struct colored_noise {
	constexpr static size_t max_fft_size = noise::Exdata::max_fft_size;

protected:
	// maximum size is doubled to make use of the cached sin/cos table.
	using FFT = sigma_lib::fft::FFT<2 * max_fft_size, float>;
	static inline std::unique_ptr<FFT> fft{};
	static void init_fft() { if (!fft) fft = std::make_unique<FFT>(); }

	static FFT::cpx* fft_buf() { return reinterpret_cast<FFT::cpx*>(memory_ptr); }
	static float* wt_tbl(uint32_t fft_size) { return reinterpret_cast<float*>(fft_buf() + 2 * fft_size); }

	static void prepare_weight_table(uint32_t fft_size, float alpha, float scale)
	{
		auto const wt = wt_tbl(fft_size);

		// pre-calculate the bias of the colored noise.
		float power = 0;
		for (size_t i = 0; i < fft_size / 2; i++) {
			auto const r = std::pow(0.5f + i, -alpha / 2);
			wt[i] = r;
			power += r * r;
		}

		// normalize the power.
		power = scale / std::sqrt(power);
		for (size_t i = 0; i < fft_size / 2; i++) wt[i] *= power;
	}

public:
	static inline void* memory_ptr = nullptr;
};
struct gaussian_noise : colored_noise {
	gaussian_noise(float alpha, uint32_t fft_size, uint32_t seed, uint_fast64_t pos, size_t alt = 0)
		: alpha{ alpha }
		, fft_size{ alpha == 0 ? 2 /* to let `get_index()` always return 0 */ : fft_size }
		, buf{ alpha == 0 ? reinterpret_cast<float*>(memory_ptr) + alt :
			(wt_tbl(fft_size) + (fft_size / 2)) + (1 + fft_size) * alt + 1 }
		, pos{ pos }, rng{ seed }
		, red_bits{ std::bit_width(max_fft_size) - std::bit_width(fft_size) }
	{
		if (alpha == 0) {
			// white noise.
			// adjust the position and cache the current value.
			rng.discard(pos);
			curr_value() = rng();
		}
		else {
			// colored noise other than white. prepare for FFT.
			init_fft();

			// pre-calculate the weight table.
			if (alt == 0) prepare_weight_table(fft_size, alpha, 0.5f);

			// expand values to buf; needs two passes.
			std::memset(buf + (fft_size / 2), 0, (fft_size / 2) * sizeof(float));
			rng.discard((2 * pos) & (0uLL - fft_size));
			batch(); // pass 1.
			batch(); // pass 2.
		}
	}

	float value() const { return curr_value(); }
	void move_next() {
		pos++;
		move_next_core();
	}

	float const alpha;
	uint32_t const fft_size;
	uint_fast64_t pos;
	normal_rng<float> rng;
	float* const buf;

private:
	int const red_bits; // number of bits reduced from the maximum.

	// call this *after* incrementing pos.
	void move_next_core() {
		if (alpha == 0) curr_value() = rng();
		else if (get_index(pos) == 0) batch();
	}
	float& curr_value() const { return buf[get_index(pos)]; }
	size_t get_index(uint_fast64_t p) const { return p & ((fft_size / 2) - 1); }

	void batch()
	{
		// assumes alpha is nonzero.
		// set random values to the frequency space.
		auto const* const wt = wt_tbl(fft_size);
		auto const buf1 = fft_buf(), buf2 = buf1 + fft_size;
		for (size_t i = 0; i < fft_size / 2; i++) {
			buf1[i] = { wt[i] * rng(), wt[i] * rng() };
			buf1[fft_size - 1 - i] = std::conj(buf1[i]); // equivalent to discarding .imag() of the output.
		}

		// perform inverse FFT.
		auto const ptr = fft->inv(buf1, buf2, fft_size);

		// place the values to the destination buffer.
		for (size_t i = 0; i < fft_size / 2; i++) {
			auto const j = i + fft_size / 2;
			// - tilt by `pi i n/N` so the frequency is shifted by 0.5.
			// - only the real part is in interest.
			// - glue with the half of the previous section, using the square root of Hann function.
			auto const& q = fft->q(i << red_bits);
			buf[i] = q.imag() * (
				// \Re(q p_i)
				q.real() * ptr[i].real() - q.imag() * ptr[i].imag())
				+ buf[j];
			buf[j] = q.real() * (
				// \Re(\sqrt{-1}q p_j)
				-q.imag() * ptr[j].real() - q.real() * ptr[j].imag());
		}
	}
};

struct velvet_noise : colored_noise {
	velvet_noise(uint32_t period, float alpha, uint32_t fft_size, uint32_t seed,
		uint_fast64_t pos, uint_fast64_t count_period, double phase_period, size_t alt = 0)
		: period{ period }, fft_size{ fft_size }, alpha{ alpha }
		, pos{ pos }, count_period{ count_period }
		, pos_period{ static_cast<uint32_t>(std::clamp<int>(std::lround(phase_period * period), 0, period - 1)) }
		, rng{ seed ^ philox::default_seed }
		, buf{ alpha == 0 ? nullptr :
			(wt_tbl(fft_size) + (fft_size / 2)) + fft_size * alt }
		, red_bits{ std::bit_width(max_fft_size) - std::bit_width(fft_size) }
	{
		if (alpha == 0) {
			rng.discard(max_fft_size + count_period);
			set_next();
		}
		else {
			// colored noise other than white. prepare for FFT.
			init_fft();

			// pre-calculate the weight table.
			if (alt == 0) prepare_weight_table(fft_size, alpha,
				1 / std::sqrtf(static_cast<float>(2 * fft_size)));

			// prepare output buffer. adjust positions.
			uint_fast64_t rng_pos = max_fft_size + count_period;
			uint32_t pos_period_0 = pos_period;
			auto const pos_residue = pos & (fft_size / 2 - 1);
			if (pos_residue + fft_size / 2 > pos_period) {
				auto const l = pos_residue + fft_size / 2 - pos_period + period - 1;
				rng_pos -= l / period;
				pos_period_0 = (period - 1) - (l % period);
			}

			// expand values to buf; needs two passes.
			rng.discard(rng_pos);
			batch(pos_period_0);
			batch((pos_period_0 + fft_size / 2) % period);
		}
	}

	uint32_t const period, fft_size;
	float const alpha;
	uint_fast64_t pos, count_period;

	float value() const {
		if (alpha == 0)
			return pos_period == pos_pulse ? val_pulse : 0.0f;
		else return buf[get_index(pos)];
	}
	void move_next() {
		pos++; pos_period++;
		if (pos_period == period) {
			pos_period = 0;
			count_period++;
		}

		if (alpha == 0) {
			if (pos_period == 0) set_next();
		}
		else {
			if (get_index(pos) == 0) batch(pos_period);
		}
	}
	double phase_period() const { return pos_period / static_cast<double>(period); }

private:
	int const red_bits; // number of bits reduced from the maximum.

	using philox = sigma_lib::rng::philox_test::philox4x32;
	uint32_t pos_period;
	philox rng;
	uint32_t pos_pulse;
	float val_pulse;
	float* const buf;

	size_t get_index(uint_fast64_t p) const { return p & ((fft_size / 2) - 1); }

	void set_next() { std::tie(pos_pulse, val_pulse) = parse_rand(rng()); }
	void batch(uint_fast64_t pos_period_0)
	{
		// assumes alpha is nonzero.
		auto rng1 = rng;

		// estimate the previous batch state.
		uint32_t pos_period_1;
		if (pos_period_0 >= fft_size / 2) pos_period_1 = (pos_period_0 - fft_size / 2) % period;
		else {
			auto const l = fft_size / 2 - pos_period_0 + period - 1;
			rng.discard(l / period);
			pos_period_1 = (period - 1) - (l % period);
		}

		// set velvet noise to the time space.
		auto const buf1 = fft_buf(), buf2 = buf1 + fft_size;
		std::memset(buf1, 0, sizeof(FFT::cpx) * fft_size);
		auto [pos_pulse_1, val_pulse_1] = parse_rand(rng1());
		for (size_t i = 0; i < fft_size; i++) {
			if (pos_pulse_1 == pos_period_1)
				// - tilt by `-pi i n/N` so the frequency is shifted by 0.5.
				// - taking the complex conjugate to adapt to inverse FFT.
				buf1[i] = val_pulse_1 * fft->q(i << red_bits);

			// determine the next position of the pulse.
			if (++pos_period_1 == period) {
				pos_period_1 = 0;
				std::tie(pos_pulse_1, val_pulse_1) = parse_rand(rng1());
			}
		}

		// perform FFT.
		auto ptr = fft->inv(buf1, buf2, fft_size);

		// modify values in the frequency space.
		auto const* const wt = wt_tbl(fft_size);
		for (size_t i = 0; i < fft_size / 2; i++) {
			// bias of the colored noise.
			auto const v = wt[i] * ptr[i];

			// taking the complex conjugate to adapt the former inverse FFT.
			buf1[i] = std::conj(v);
			buf1[fft_size - 1 - i] = v;
		}

		// perform inverse FFT.
		ptr = fft->inv(buf1, buf2, fft_size);

		// place the values to the destination buffer.
		for (size_t i = 0; i < fft_size / 2; i++) {
			auto const j = i + fft_size / 2;
			// - tilt back by `pi i n/N`.
			// - only the real part is in interest.
			// - glue with the half of the previous section by Hann function.
			auto const& q = fft->q(i << red_bits);
			auto const hann = q.imag() * q.imag();
			buf[i] = hann * (
				// \Re(q p_i)
				q.real() * ptr[i].real() - q.imag() * ptr[i].imag())
				+ buf[j];
			buf[j] = (1 - hann) * (
				// \Re(\sqrt{-1}q p_j)
				-q.imag() * ptr[j].real() - q.real() * ptr[j].imag());
		}
	}

	std::pair<uint32_t, float> parse_rand(uint32_t r) const { return parse_rand(r, period); }
	constexpr static std::pair<uint32_t, float> parse_rand(uint32_t r, uint32_t period) {
		return {
			static_cast<uint32_t>((period * static_cast<uint64_t>(r & ~(1u << 31))) >> 31),
			(r & (1u << 31)) != 0 ? -1.0f : 1.0f
		};
	}
};


////////////////////////////////
// フィルタ処理．
////////////////////////////////
static inline double calc_hertz(double halftone)
{
	return 440 * std::exp2(halftone / 12); // originates A with 440 Hz.
}

static inline float calc_volume(double decibel)
{
	return static_cast<float>(std::exp((0.05 / std::numbers::log10e) * decibel));
}

static inline uint32_t calc_seed(int32_t seed, ExEdit::Filter const* efp)
{
	// negative seeds are independent of objects,
	// whereas nonnegatives are dependent.
	return seed < 0 ? ~seed :
		seed ^ static_cast<uint32_t>(efp->exfunc->get_start_idx(efp->processing));
}

struct gaussian_noise_state {
	uint64_t pos;
	double phase;

	constexpr bool is_default() const { return pos == 0 && phase == 0; }
	constexpr auto& normalize() {
		phase = std::isfinite(phase) && 0 < phase && phase < 1 ? phase : 0;
		return *this;
	}
	constexpr void rewind_one() {
		// rewind the state by one step to adapt read-forward behavior for interpolation.
		pos--;
	}
};

struct velvet_noise_state {
	uint64_t pos;
	double phase;
	uint64_t count_period;
	double phase_period;

	constexpr bool is_default() const { return pos == 0 && phase == 0 && count_period == 0 && phase_period == 0; }
	constexpr auto& normalize() {
		phase = std::isfinite(phase) && 0 < phase && phase < 1 ? phase : 0;
		phase_period = std::isfinite(phase_period) && 0 < phase_period && phase_period < 1 ? phase_period : 0;
		return *this;
	}
	constexpr void rewind_one(uint32_t period) {
		// rewind the state by one step to adapt read-forward behavior for interpolation.
		pos--;
		phase_period -= 1.0 / period;
		if (phase_period < 0) {
			auto i = std::floor(phase_period);
			count_period += static_cast<int_fast64_t>(i);
			phase_period -= i;
		}
	}
};

// adjusts the frequency according to the playback rate,
// and possibly reset the state of noise.
template<class noise_state>
static inline std::pair<double, noise_state*> adjust_pos_phase(double hertz, ExEdit::Filter const* efp, ExEdit::FilterProcInfo const* efpip)
{
	noise_state state{};
	double delta_phase = hertz / efpip->audio_rate;

	// find cache to recall the previous pos and phase.
	struct state_cache {
		noise_state curr, prev;
		int32_t prev_milliframe;
	};
	int cache_exists_flag;
	state_cache* const cache = reinterpret_cast<state_cache*>(exedit.get_or_create_cache(
		efp->processing, sizeof(state_cache) / alignof(state_cache), 1, 8 * alignof(state_cache),
		0, &cache_exists_flag));

	// ref: https://github.com/nazonoSAUNA/simple_wave.eef/blob/main/src.cpp
	auto curr_milliframe = 1000 * (efpip->frame + efpip->add_frame);
	bool is_head = false;
	if (efpip->audio_speed != 0) {
		curr_milliframe = efpip->audio_milliframe - 1000 * (efpip->frame_num - efpip->frame);
		auto const
			speed = 0.000'001 * efpip->audio_speed,
			frame = 0.001 * curr_milliframe;

		delta_phase *= std::abs(speed);
		if (speed >= 0 ? frame - speed < 0 : frame - speed >= efpip->frame_n) // 想定の前回描画フレームが範囲外．
			is_head = true;
	}
	else if (curr_milliframe == 0) is_head = true; // 1フレーム目
	if (!is_head && cache_exists_flag != 0 && cache != nullptr &&
		(efpip->audio_speed >= 0 ?
			curr_milliframe >= cache->prev_milliframe :
			curr_milliframe <= cache->prev_milliframe))
		state = cache->curr;

	if (cache != nullptr) {
		// rewind the position and phase if re-rendering the same frame.
		if (cache_exists_flag != 0 && !state.is_default() &&
			curr_milliframe == cache->prev_milliframe)
			state = cache->prev;

		cache->prev_milliframe = curr_milliframe;
		cache->curr = cache->prev = state.normalize();
	}

	return { delta_phase, cache == nullptr ? nullptr : &cache->curr };
}

// helper lambda to control phasing.
static constexpr auto lambda_step_one(double& phase, double delta) {
	return [&phase, delta](auto&... args) {
		static_assert(sizeof...(args) % 2 == 0);

		phase += delta;
		if (phase >= 1) {
			phase -= std::floor(phase);
			[&](this auto&& self, auto& gen, auto& prev, auto&... rest) {
				prev = gen.value();
				gen.move_next();

				if constexpr (sizeof...(rest) > 0) self(rest...);
			}(args...);
		}
	};
}

// find a suitable address to the space for noise calculations.
static void set_noise_gen_space(ExEdit::FilterProcInfo* efpip)
{
	// *exedit.memory_ptr can be used as efpip->audio_p
	// (specifically when `obj.getaudio()` is called).
	// find the alternative for that case.
	auto mem = reinterpret_cast<uintptr_t>(*exedit.memory_ptr);
	if (reinterpret_cast<uintptr_t>(efpip->audio_p) == mem)
		mem += (efpip->audio_n * efpip->audio_ch * sizeof(int16_t)
			+ 3) & (-4); // align as 4 bytes.
	colored_noise::memory_ptr = reinterpret_cast<void*>(mem);
}

static void apply_volume(float volume, int16_t* st, int16_t const* ed)
{
	for (auto p = st; p < st; p++)
		*p = static_cast<int16_t>(std::lround(volume * *p));
}
static void apply_volume(float volume, int16_t* st, int count) { apply_volume(volume, st, st + count); }

BOOL noise::func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
{
	int constexpr
		min_alpha	= track_min		[idx_track::alpha],
		max_alpha	= track_max		[idx_track::alpha],
		den_alpha	= track_denom	[idx_track::alpha],
		min_freq	= track_min		[idx_track::resolution],
		max_freq	= track_max		[idx_track::resolution],
		den_freq	= track_denom	[idx_track::resolution],
		min_back	= track_min		[idx_track::back_volume],
		max_back	= track_max		[idx_track::back_volume],
		den_back	= track_denom	[idx_track::back_volume];

	// prepare paramters.
	int const
		raw_alpha	= efp->track	[idx_track::alpha],
		raw_freq	= efp->track	[idx_track::resolution],
		raw_back	= efp->track	[idx_track::back_volume];
	bool const
		stereo		= efp->check	[idx_check::stereo] != 0,
		interpolate	= efp->check	[idx_check::interpolate] != 0;
	Exdata* const exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);

	float const
		alpha		= std::clamp(raw_alpha, min_alpha, max_alpha) / static_cast<float>(100 * den_alpha);
	double const
		hertz		= calc_hertz(std::clamp(raw_freq, min_freq, max_freq) / static_cast<double>(den_freq));
	float const
		back_volume	= std::clamp(raw_back, min_back, max_back) / static_cast<float>(100 * den_back);
	uint32_t const
		seed		= calc_seed(exdata->seed, efp),
		fft_size	= exdata->clamped_fft_size();

	// recall previous state.
	auto [delta_phase, state_ptr] = adjust_pos_phase<gaussian_noise_state>(hertz, efp, efpip);
	auto [pos, phase] = state_ptr != nullptr ? *state_ptr : std::decay_t<decltype(*state_ptr)>{};
	constexpr double const_0 = 0;
	double const& phase_ref = interpolate ? phase : const_0;

	// generate noise.
	double const delta_phase_corr = raw_freq >= max_freq ? 1.0 : std::min(delta_phase, 1.0);
	auto step_one = lambda_step_one(phase, delta_phase_corr);
	auto val = [&phase_ref](gaussian_noise const& gen, float prev) {
		auto const t = static_cast<float>(phase_ref);
		return to_int((1 - t) * prev + t * gen.value());
	};
	set_noise_gen_space(efpip);
	int16_t* const data = efpip->audio_data;
	if (stereo && efpip->audio_ch == 2) {
		// prepare two noise generators.
		gaussian_noise
			genL{ alpha, fft_size, seed, pos },
			genR{ alpha, fft_size, ~seed, pos, 1 };
		float prevL = genL.value(), prevR = genR.value();
		genL.move_next(); genR.move_next();

		// write values to the buffer.
		for (int i = 0; i < efpip->audio_n; i++) {
			step_one(genL, prevL, genR, prevR);
			data[2 * i + 0] = val(genL, prevL);
			data[2 * i + 1] = val(genR, prevR);
		}

		// update the position.
		pos = genL.pos;
	}
	else {
		// prepare a noise generator.
		gaussian_noise gen{ alpha, fft_size, seed, pos };
		float prev = gen.value(); gen.move_next();

		// write values to the buffer.
		if (efpip->audio_ch == 2) {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				data[2 * i] = data[2 * i + 1] = val(gen, prev);
			}
		}
		else {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				data[i] = val(gen, prev);
			}
		}

		// update the position.
		pos = gen.pos;
	}

	// store the phase and the position for the next use.
	if (state_ptr != nullptr) {
		*state_ptr = { pos, phase };
		state_ptr->rewind_one();
	}

	// lower (or possibly gain) the sound already rendered.
	if (back_volume != 1.0f)
		apply_volume(back_volume, efpip->audio_p, efpip->audio_ch * efpip->audio_n);

	return TRUE;
}

BOOL noise_multiply::func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
{
	int constexpr
		min_int		= track_min		[idx_track::intensity],
		max_int		= track_max		[idx_track::intensity],
		den_int		= track_denom	[idx_track::intensity],
		min_alpha	= track_min		[idx_track::alpha],
		max_alpha	= track_max		[idx_track::alpha],
		den_alpha	= track_denom	[idx_track::alpha],
		min_freq	= track_min		[idx_track::resolution],
		max_freq	= track_max		[idx_track::resolution],
		den_freq	= track_denom	[idx_track::resolution],
		min_bound	= track_min		[idx_track::lo_bound],
		max_bound	= track_max		[idx_track::lo_bound],
		den_bound	= track_denom	[idx_track::lo_bound];

	// prepare paramters.
	int const
		raw_int		= efp->track	[idx_track::intensity],
		raw_alpha	= efp->track	[idx_track::alpha],
		raw_freq	= efp->track	[idx_track::resolution],
		raw_ubound	= efp->track	[idx_track::up_bound],
		raw_lbound	= efp->track	[idx_track::lo_bound];
	bool const
		stereo		= efp->check	[idx_check::stereo] != 0,
		interpolate	= efp->check	[idx_check::interpolate] != 0,
		invert		= efp->check	[idx_check::invert];
	Exdata* const exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);

	float const
		intensity	= std::clamp(raw_int, min_int, max_int) / static_cast<float>(100 * den_int),
		alpha		= std::clamp(raw_alpha, min_alpha, max_alpha) / static_cast<float>(100 * den_alpha);
	double const
		hertz		= calc_hertz(std::clamp(raw_freq, min_freq, max_freq) / static_cast<double>(den_freq));
	float const
		u_bound		= raw_ubound <= min_bound ? 0 :
			calc_volume(std::clamp(raw_ubound, min_bound, max_bound) / static_cast<double>(den_bound)),
		l_bound		= raw_lbound <= min_bound ? 0 :
			calc_volume(std::clamp(raw_lbound, min_bound, max_bound) / static_cast<double>(den_bound));
	uint32_t const
		seed		= calc_seed(exdata->seed, efp),
		fft_size	= exdata->clamped_fft_size();

	// recall previous state.
	auto [delta_phase, state_ptr] = adjust_pos_phase<gaussian_noise_state>(hertz, efp, efpip);
	auto [pos, phase] = state_ptr != nullptr ? *state_ptr : std::decay_t<decltype(*state_ptr)>{};
	constexpr double const_0 = 0;
	double const& phase_ref = interpolate ? phase : const_0;

	// filter by noise.
	double const delta_phase_corr = raw_freq >= max_freq ? 1.0 : std::min(delta_phase, 1.0);
	auto step_one = lambda_step_one(phase, delta_phase_corr);
	auto val = [&phase_ref](gaussian_noise const& gen, float prev) {
		auto const t = static_cast<float>(phase_ref);
		return (1 - t) * prev + t * gen.value();
	};
	auto bound = [=, dyn_range = std::max(u_bound - l_bound, 0.0f)](float noise) {
		auto const ret = dyn_range <= 0 ?
			std::abs(noise) <= l_bound ? 0.0f : 1.0f :
			std::clamp((std::abs(noise) - l_bound) / dyn_range, 0.0f, 1.0f);
		return invert ? 1 - ret : noise >= 0 ? ret : -ret;
	};
	auto mix = [&](float noise, auto&... signal) {
		float const rate = (1 - intensity) + intensity * bound(noise);
		((signal = static_cast<int16_t>(std::lround(rate * signal))), ...);
	};
	set_noise_gen_space(efpip);
	int16_t* const data = has_flag_or(efp->flag, ExEdit::Filter::Flag::Effect) ?
		efpip->audio_data : efpip->audio_p;
	if (stereo && efpip->audio_ch == 2) {
		// prepare two noise generators.
		gaussian_noise
			genL{ alpha, fft_size, seed, pos },
			genR{ alpha, fft_size, ~seed, pos, 1 };
		float prevL = genL.value(), prevR = genR.value();
		genL.move_next(); genR.move_next();

		// write values to the buffer.
		for (int i = 0; i < efpip->audio_n; i++) {
			step_one(genL, prevL, genR, prevR);
			mix(val(genL, prevL), data[2 * i + 0]);
			mix(val(genR, prevR), data[2 * i + 1]);
		}

		// update the position.
		pos = genL.pos;
	}
	else {
		// prepare a noise generator.
		gaussian_noise gen{ alpha, fft_size, seed, pos };
		float prev = gen.value(); gen.move_next();

		// write values to the buffer.
		if (efpip->audio_ch == 2) {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				mix(val(gen, prev), data[2 * i], data[2 * i + 1]);
			}
		}
		else {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				mix(val(gen, prev), data[i]);
			}
		}

		// update the position.
		pos = gen.pos;
	}

	// store the phase and the position for the next use.
	if (state_ptr != nullptr) {
		*state_ptr = { pos, phase };
		state_ptr->rewind_one();
	}

	return TRUE;
}

BOOL velvet::func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
{
	int constexpr
		min_fuzzy	= track_min		[idx_track::fuzzy],
		max_fuzzy	= track_max		[idx_track::fuzzy],
		den_fuzzy	= track_denom	[idx_track::fuzzy],
		min_alpha	= track_min		[idx_track::alpha],
		max_alpha	= track_max		[idx_track::alpha],
		den_alpha	= track_denom	[idx_track::alpha],
		min_freq	= track_min		[idx_track::resolution],
		max_freq	= track_max		[idx_track::resolution],
		den_freq	= track_denom	[idx_track::resolution],
		min_back	= track_min		[idx_track::back_volume],
		max_back	= track_max		[idx_track::back_volume],
		den_back	= track_denom	[idx_track::back_volume];

	// prepare paramters.
	int const
		raw_fuzzy	= efp->track	[idx_track::fuzzy],
		raw_alpha	= efp->track	[idx_track::alpha],
		raw_freq	= efp->track	[idx_track::resolution],
		raw_back	= efp->track	[idx_track::back_volume];
	bool const
		stereo		= efp->check	[idx_check::stereo] != 0,
		interpolate = efp->check[idx_check::interpolate] != 0;
	Exdata* const exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);

	double const
		taps_hertz	= calc_hertz(std::clamp(raw_fuzzy, min_fuzzy, max_fuzzy) / static_cast<double>(den_fuzzy));
	float const
		alpha		= std::clamp(raw_alpha, min_alpha, max_alpha) / static_cast<float>(100 * den_alpha);
	double const
		hertz		= calc_hertz(std::clamp(raw_freq, min_freq, max_freq) / static_cast<double>(den_freq));
	float const
		back_volume	= std::clamp(raw_back, min_back, max_back) / static_cast<float>(100 * den_back);
	uint32_t const
		seed		= calc_seed(exdata->seed, efp),
		fft_size	= exdata->clamped_fft_size();

	// recall previous state.
	auto [delta_phase, state_ptr] = adjust_pos_phase<velvet_noise_state>(hertz, efp, efpip);
	auto [pos, phase, count_period, phase_period] = state_ptr != nullptr ? *state_ptr : std::decay_t<decltype(*state_ptr)>{};
	constexpr double const_0 = 0;
	double const& phase_ref = interpolate ? phase : const_0;

	// generate noise.
	double const delta_phase_corr = raw_freq >= max_freq ? 1.0 : std::min(delta_phase, 1.0);
	uint32_t const period = raw_fuzzy >= max_fuzzy ? 1 :
		std::max<uint32_t>(std::lround(delta_phase_corr * efpip->audio_rate / taps_hertz), 1);
	auto step_one = lambda_step_one(phase, delta_phase_corr);
	auto val = [&phase_ref](velvet_noise const& gen, float prev) {
		auto const t = static_cast<float>(phase_ref);
		return to_int((1 - t) * prev + t * gen.value());
	};
	set_noise_gen_space(efpip);
	int16_t* const data = efpip->audio_data;
	if (stereo && efpip->audio_ch == 2) {
		// prepare two noise generators.
		velvet_noise
			genL{ period, alpha, fft_size, seed, pos, count_period, phase_period },
			genR{ period, alpha, fft_size, ~seed, pos, count_period, phase_period, 1 };
		float prevL = genL.value(), prevR = genR.value();
		genL.move_next(); genR.move_next();

		// write values to the buffer.
		for (int i = 0; i < efpip->audio_n; i++) {
			step_one(genL, prevL, genR, prevR);
			data[2 * i + 0] = val(genL, prevL);
			data[2 * i + 1] = val(genR, prevR);
		}

		// update the states.
		pos = genL.pos;
		count_period = genL.count_period;
		phase_period = genL.phase_period();
	}
	else {
		// prepare a noise generator.
		velvet_noise gen{ period, alpha, fft_size, seed, pos, count_period, phase_period };
		float prev = gen.value(); gen.move_next();

		// write values to the buffer.
		if (efpip->audio_ch == 2) {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				data[2 * i] = data[2 * i + 1] = val(gen, prev);
			}
		}
		else {
			for (int i = 0; i < efpip->audio_n; i++) {
				step_one(gen, prev);
				data[i] = val(gen, prev);
			}
		}

		// update the states.
		pos = gen.pos;
		count_period = gen.count_period;
		phase_period = gen.phase_period();
	}

	// store the states for the next use.
	if (state_ptr != nullptr) {
		*state_ptr = { pos, phase, count_period, phase_period };
		state_ptr->rewind_one(period);
	}

	// lower (or possibly gain) the sound already rendered.
	if (back_volume != 1.0f)
		apply_volume(back_volume, efpip->audio_p, efpip->audio_ch * efpip->audio_n);

	return TRUE;
}

BOOL pulse::func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip)
{
	int constexpr
		min_pos		= track_min		[idx_track::position],
		max_pos		= track_max		[idx_track::position],
		min_dur		= track_min		[idx_track::duration],
		max_dur		= track_max		[idx_track::duration],
		den_time	= track_denom	[idx_track::position],
		min_back	= track_min		[idx_track::back_volume],
		max_back	= track_max		[idx_track::back_volume],
		den_back	= track_denom	[idx_track::back_volume];

	// prepare paramters.
	int const
		raw_pos		= efp->track	[idx_track::position],
		raw_dur		= efp->track	[idx_track::duration],
		raw_back	= efp->track	[idx_track::back_volume];

	double const
		pos = static_cast<double>(efpip->audio_rate)
			* std::clamp(raw_pos, min_pos, max_pos) / (1000 * den_time),
		dur = static_cast<double>(efpip->audio_rate)
			* std::clamp(raw_dur, min_dur, max_dur) / (1000 * den_time);
	float const
		back_volume	= std::clamp(raw_back, min_back, max_back) / static_cast<float>(100 * den_back);

	// take the playback rate into account.
	double speed = 1, frame = efpip->frame + efpip->add_frame;
	// ref: https://github.com/nazonoSAUNA/simple_wave.eef/blob/main/src.cpp
	if (efpip->audio_speed != 0) {
		speed = 0.000'001 * efpip->audio_speed;
		frame = 0.001 * efpip->audio_milliframe - (efpip->frame_num - efpip->frame);
	}

	// determine the position of the buffer.
	int const
		duration	= std::max(1, static_cast<int>(dur / std::abs(speed))),
		pos_start	= static_cast<int>((pos - frame * efpip->audio_rate * efpip->framerate_de / efpip->framerate_nu) / speed),
		pos_end		= pos_start + (speed >= 0 ? duration : -duration),
		i_s			= efpip->audio_ch * std::max(std::min(pos_start, pos_end), 0),
		i_e			= efpip->audio_ch * std::min(std::max(pos_start, pos_end), efpip->audio_n),
		N			= efpip->audio_ch * efpip->audio_n;

	// write/modify the audio buffer.
	int16_t* const data = efpip->audio_data;
	if (i_e <= 0 || N <= i_s) std::memset(data, 0, N * sizeof(int16_t));
	else {
		std::memset(data + 0, 0, i_s * sizeof(int16_t));
		std::fill(data + i_s, data + i_e, pulse_height);
		std::memset(data + i_e, 0, (N - i_e) * sizeof(int16_t));

		// lower (or possibly gain) the sound already rendered.
		if (back_volume != 1.0f)
			apply_volume(back_volume, efpip->audio_p + i_s, efpip->audio_p + i_e);
	}

	return TRUE;
}


////////////////////////////////
// 初期化．
////////////////////////////////
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		::DisableThreadLibraryCalls(hinst);
		break;
	}
	return TRUE;
}


////////////////////////////////
// エントリポイント．
////////////////////////////////
EXTERN_C __declspec(dllexport) ExEdit::Filter* const* __stdcall GetFilterTableList() {
	constexpr static ExEdit::Filter* filter_list[] = {
		&noise::filter,
		&noise_multiply::effect,
		&noise_multiply::filter,
		&velvet::filter,
		&pulse::filter,
		nullptr,
	};

	return filter_list;
}
