/*
The MIT License (MIT)

Copyright (c) 2025 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdint>
#include <limits>
#include <array>

namespace sigma_lib::rng::philox_test
{
	/// @brief test version of Philox engine.
	///
	/// https://wg21.link/P2075R6
	///
	/// has limitations:
	///
	/// - the template parameter `w` must be 32 or 64,
	///   and exactly fit with the size of `UIntType`,
	///
	/// - "required behaviors" below for `philox4x32` and `philox4x64`
	///   do NOT satisfy for some reason unknown.
	/// 
	/// - does not have inserters and extractors.
	template<class UIntType, size_t w, size_t n, size_t r, UIntType... consts>
	struct philox_engine_test {
		static_assert(
			sizeof...(consts) == n &&
			(n == 2 || n == 4 || n == 8 || n == 16) &&
			0 < r &&
			//0 < w && w <= std::numeric_limits<UIntType>::digits &&
			(w == 32 || w == 64) && w == std::numeric_limits<UIntType>::digits);

		// types
		using result_type = UIntType;

		// engine characteristics
		constexpr static size_t word_size = w;
		constexpr static size_t word_count = n;
		constexpr static size_t round_count = r;

	private:
		constexpr static size_t array_size = word_count / 2; // exposition only.

		template<size_t m, size_t r>
		static consteval std::array<result_type, word_count / m> skipped_array()
		{
			std::array<result_type, word_count / m> ret{};
			std::array<result_type, word_count> const src{ consts... };
			for (size_t k = 0; k < word_count / m; k++)
				ret[k] = src[k * m + r];
			return ret;
		}

	public:
		constexpr static std::array<result_type, array_size> multipliers = skipped_array<2, 0>();
		constexpr static std::array<result_type, array_size> round_consts = skipped_array<2, 1>();
		static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
		static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
		constexpr static result_type default_seed = 20111115u;

		// constructors and seeding functions
		constexpr philox_engine_test() : philox_engine_test(default_seed) {}
		constexpr explicit philox_engine_test(result_type value)
		{
			seed(value);
		}
		template<class seed_seq> constexpr explicit philox_engine_test(seed_seq& q)
		{
			seed(q);
		}
		constexpr void seed(result_type value = default_seed)
		{
			K[0] = value;
			for (size_t k = 1; k < array_size; k++) K[k] = 0;

			// reset X and i.
			X = {};
			i = word_count - 1;
		}
		template<class seed_seq> constexpr void seed(seed_seq& q)
		{
			// assuming little endian.
			q.generate(std::begin(K), std::end(K));

			// reset X and i.
			X = {};
			i = word_count - 1;
		}

		constexpr void set_counter(std::array<result_type, word_count> const& counter)
		{
			std::copy(counter.rbegin(), counter.rend(), X);
		}

		// equality operators
		constexpr bool operator==(philox_engine_test const& other) const
		{
			return i == other.i && K == other.K && X == other.X;
		}

		// generating functions
		constexpr result_type operator()()
		{
			i++;
			if (i == word_count) {
				Philox();

				increment_Z();
				i = 0;
			}

			// return the generated value.
			return Y[i];
		}
		constexpr void discard(uint64_t z)
		{
			z += i;
			if (z < word_count) {
				// discarding small amount.
				i = static_cast<size_t>(z);
				return;
			}

			// calculate the rest.
			i = z % word_count;
			z /= word_count;

			if (i != word_count - 1) z--; // operator() calls Philox() *before* Z increments.

			// add to Z.
			for (size_t j = 0; j < word_count && z > 0; j++) {
				auto z0 = static_cast<result_type>(z);
				z >>= word_size;
				if ((X[j] += z0) < z0) z++;
			}

			// generate Y if necessary.
			if (i != word_count - 1) {
				Philox();
				increment_Z(); // do the pended increment.
			}
		}

	private:
		// implementations of PRNG.
		std::array<result_type, array_size> K; // "key" sequence (essentially a seed).
		std::array<result_type, word_count> X; // represents a big integer Z = \sum_j 2^{w j} X_j.
		std::array<result_type, word_count> Y; // generated sequence.
		size_t i = 0;

		constexpr void increment_Z() {
			for (size_t j = 0; j < word_count && ++X[j] == 0; ++j);
		}

		constexpr void Philox()
		{
			Y = X;
			for (size_t q = 0; q < round_count; q++)
				philox_round(q);
		}
		constexpr void philox_round(size_t q)
		{
			auto const V = permute(Y);

			for (size_t k = 0; k < array_size; k++) {
				mul_lo_hi(Y[2 * k], Y[2 * k + 1], V[2 * k + 1], multipliers[k]);
				Y[2 * k + 1] ^= (K[k] + q * round_consts[k]) ^ V[2 * k];
			}
		}

		static constexpr std::array<result_type, 2> permute(std::array<result_type, 2> const& X) {
			return X;
		}
		static constexpr std::array<result_type, 4> permute(std::array<result_type, 4> const& X) {
			return {
				X[0],
				X[3],
				X[2],
				X[1],
			};
		}
		static constexpr std::array<result_type, 8> permute(std::array<result_type, 8> const& X) {
			return {
				X[2],
				X[1],
				X[4],
				X[7],
				X[6],
				X[5],
				X[0],
				X[3],
			};
		}
		static constexpr std::array<result_type, 16> permute(std::array<result_type, 16> const& X) {
			return {
				X[ 0],
				X[ 9],
				X[ 2],
				X[13],
				X[ 6],
				X[11],
				X[ 4],
				X[15],
				X[10],
				X[ 7],
				X[12],
				X[ 3],
				X[14],
				X[ 5],
				X[ 8],
				X[ 1],
			};
		}

		static constexpr void mul_lo_hi(result_type& lo, result_type& hi, result_type a, result_type b)
		{
			if constexpr (word_size == 32) {
				uint_fast64_t const x = static_cast<uint_fast64_t>(a) * b;
				lo = static_cast<result_type>(x);
				hi = static_cast<result_type>(x >> 32);
			}
			else if constexpr (word_size == 64) {
				constexpr uint_fast64_t low = (1uLL << 32) - 1;
				uint_fast64_t const
					a0 = static_cast<uint_fast64_t>(a) & low, a1 = static_cast<uint_fast64_t>(a) >> 32,
					b0 = static_cast<uint_fast64_t>(b) & low, b1 = static_cast<uint_fast64_t>(b) >> 32;
				uint_fast64_t
					m00 = a0 * b0, m01 = a0 * b1, m10 = a1 * b0, m11 = a1 * b1;
				m01 += (m00 >> 32) + (m10 & low);
				m00 = (m00 & low) | (m01 << 32);
				m11 += (m01 >> 32) + (m10 >> 32);

				lo = static_cast<result_type>(m00);
				hi = static_cast<result_type>(m11);
			}
		}
	};

	using philox4x32 = philox_engine_test<uint_fast32_t, 32, 4, 10, 0xD2511F53, 0x9E3779B9, 0xCD9E8D57, 0xBB67AE85>;
	using philox4x64 = philox_engine_test<uint_fast64_t, 64, 4, 10, 0xD2E7470EE14C6C93, 0x9E3779B97F4A7C15, 0xCA5A826395121157, 0xBB67AE8584CAA73B>;
	/*
	Required behaviors, which do NOT satisfy for some reason unknown:
		philox4x32:
			The 10000th consecutive invocation of a default-constructed object of type
			philox4x32 produces the value 1955073260.

		philox4x64:
			The 10000th consecutive invocation of a default-constructed object of type
			philox4x64 produces the value 3409172418970261260.
	*/
}
