/*
The MIT License (MIT)

Copyright (c) 2025 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <numbers>
#include <complex>
#include <bit>
#include <concepts>


namespace sigma_lib::fft
{
	namespace detail
	{
		constexpr auto flat_loop = [](size_t N, std::invocable<size_t const> auto&& lambda) -> void {
			for (size_t i = 0; i < N; i++) lambda(i);
		};
	}
	template<size_t max_N, std::floating_point base_float = float, auto do_loop = detail::flat_loop>
		requires(max_N > 2 && std::popcount(max_N) == 1)
	struct FFT {
		constexpr static size_t
			max_size = max_N;

		using cpx = std::complex<base_float>;

	private:
		constexpr static size_t max_bits = std::bit_width(max_size) - 1;
		cpx Q[max_size / 2];
		constexpr static bool is_size_valid(size_t N) {
			// assuming max_size is a power of 2,
			// the below is equivalent to: N > 0 && max_size % N == 0.
			return 0 < N && N <= max_size && (N & (N - 1)) == 0;
		}

	public:
		constexpr FFT()
		{
			constexpr auto pi = std::numbers::pi_v<base_float>;
			do_loop(max_size / 4, [this](size_t const n) {
				auto const arg = 2 * pi * n / max_size;
				auto const re = std::cos(arg), im = std::sin(arg);
				// q[n] = \e(n/N), where \e(z) = exp(2 pi i z).
				Q[n] = { re, im };
				Q[n + max_size / 4] = { -im, re };
			});
		}

		// @return the buffer containing the image of the Fourier transform,
		// which is either `src` or `buf`. `src` if `N` is a power of 4, `buf` otherwise.
		template<size_t N> requires(is_size_valid(N))
		constexpr cpx(&operator()(cpx(&src)[N], cpx(&buf)[N]) const)[N]
		{
			cpx(*s)[N] = &src;
			cpx(*d)[N] = &buf;
			for (size_t b = 1, B = max_bits - 1; b < N; b <<= 1, B--, std::swap(s, d)) {
				do_loop(N / 2, [&, this](size_t const n) {
					size_t const
						m = n & (b - 1),
						s0 = n, s1 = s0 | (N / 2),
						d0 = ((n ^ m) << 1) | m,
						d1 = d0 | b;

					auto const t = std::conj(Q[m << B]) * (*s)[s1];
					(*d)[d0] = (*s)[s0] + t;
					(*d)[d1] = (*s)[s0] - t;
				});
			}

			return *s;
		}
		// @return the buffer containing the image of the Fourier transform,
		// which is either `src` or `buf`. `src` if `N` is a power of 4, `buf` otherwise.
		constexpr cpx* operator()(cpx* src, cpx* buf, size_t N) const
		{
			if (!is_size_valid(N)) return src;

			cpx* s = src;
			cpx* d = buf;
			for (size_t b = 1, B = max_bits - 1; b < N; b <<= 1, B--, std::swap(s, d)) {
				do_loop(N / 2, [&, this](size_t const n) {
					size_t const
						m = n & (b - 1),
						s0 = n, s1 = s0 | (N / 2),
						d0 = ((n ^ m) << 1) | m,
						d1 = d0 | b;

					auto const t = std::conj(Q[m << B]) * s[s1];
					d[d0] = s[s0] + t;
					d[d1] = s[s0] - t;
				});
			}

			return s;
		}
		// @brief a proxy of operator(), which operates a Fourier transform.
		template<size_t N> requires(is_size_valid(N))
		constexpr auto& fwd(cpx(&src)[N], cpx(&buf)[N]) const { return (*this)(src, buf); }
		// @brief a proxy of operator(), which operates a Fourier transform.
		constexpr auto* fwd(cpx* src, cpx* buf, size_t N) const { return (*this)(src, buf, N); }

		// @return the buffer containing the image of the Fourier transform,
		// which is either `src` or `buf`. `src` if `N` is a power of 4, `buf` otherwise.
		template<size_t N> requires(is_size_valid(N))
		constexpr cpx(&inv(cpx(&src)[N], cpx(&buf)[N]) const)[N]
		{
			cpx(*s)[N] = &src;
			cpx(*d)[N] = &buf;
			for (size_t b = 1, B = max_bits - 1; b < N; b <<= 1, B--, std::swap(s, d)) {
				do_loop(N / 2, [&, this](size_t const n) {
					size_t const
						m = n & (b - 1),
						s0 = n, s1 = s0 | (N / 2),
						d0 = ((n ^ m) << 1) | m,
						d1 = d0 | b;

					auto const t = Q[m << B] * (*s)[s1];
					(*d)[d0] = (*s)[s0] + t;
					(*d)[d1] = (*s)[s0] - t;
				});
			}

			return *s;
		}
		// @return the buffer containing the image of the Fourier transform,
		// which is either `src` or `buf`. `src` if `N` is a power of 4, `buf` otherwise.
		constexpr cpx* inv(cpx* src, cpx* buf, size_t N) const
		{
			if (!is_size_valid(N)) return src;

			cpx* s = src;
			cpx* d = buf;
			for (size_t b = 1, B = max_bits - 1; b < N; b <<= 1, B--, std::swap(s, d)) {
				do_loop(N / 2, [&, this](size_t const n) {
					size_t const
						m = n & (b - 1),
						s0 = n, s1 = s0 | (N / 2),
						d0 = ((n ^ m) << 1) | m,
						d1 = d0 | b;

					auto const t = Q[m << B] * s[s1];
					d[d0] = s[s0] + t;
					d[d1] = s[s0] - t;
				});
			}

			return s;
		}

		constexpr auto const& q() const { return Q; }
		constexpr cpx const& q(size_t i) const { return Q[i]; }
	};
}
