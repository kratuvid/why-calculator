#include "arbit.hpp"

namespace wc
{
	arbit::arbit(const arbit& other)
	{
		*this = other;
	}

	arbit::arbit(arbit&& other)
		:precision(other.precision),
		 fixed_len(other.fixed_len), decimal_len(other.decimal_len),
		 actual_fixed_len(other.actual_fixed_len), actual_decimal_len(other.actual_decimal_len)
	{
		if (fixed_ptr) {
			internal_free(fixed_ptr);
			fixed_ptr = nullptr;
		}
		if (decimal_ptr) {
			internal_free(decimal_ptr);
			decimal_ptr = nullptr;
		}

		fixed_ptr = other.fixed_ptr;
		decimal_ptr = other.decimal_ptr;

		other.fixed_ptr = other.decimal_ptr = nullptr;
		other.fixed_len = other.decimal_len = 0;
		other.actual_fixed_len = other.actual_decimal_len = 0;
		other.precision = default_precision;
	}

	arbit::arbit(std::string_view both, base_t precision)
		:precision(precision)
	{
		parse(both);
	}

	arbit::arbit(base_t fixed, base_t decimal, base_t precision)
		:arbit(std::initializer_list<base_t>({fixed}), {}, precision)
	{
	}

	arbit::~arbit()
	{
		if (fixed_ptr) internal_free(fixed_ptr);
		if (decimal_ptr) internal_free(decimal_ptr);
	}

	void arbit::reset()
	{
		if (fixed_ptr) {
			internal_free(fixed_ptr);
			fixed_ptr = nullptr;
		}
		if (decimal_ptr) {
			internal_free(decimal_ptr);
			decimal_ptr = nullptr;
		}

		precision = default_precision;
		fixed_len = decimal_len = 0;
		actual_fixed_len = actual_decimal_len = 0;
	}

	bool arbit::bit(size_t at) const
	{
		const size_t unit = at / base_bits, unit_at = at % base_bits;
		if (unit <= fixed_len-1)
			return fixed_ptr[unit] & (1 << unit_at);
		return false;
	}

	void arbit::clear_bit(size_t at)
	{
		const size_t unit = at / base_bits, unit_at = at % base_bits;
		if (unit <= fixed_len-1)
			fixed_ptr[unit] &= ~(1 << unit_at);
	}

	void arbit::set_bit(size_t at)
	{
		const size_t unit = at / base_bits, unit_at = at % base_bits;
		if (unit <= fixed_len-1)
			fixed_ptr[unit] |= (1 << unit_at);
	}

	void arbit::flip_bit(size_t at)
	{
		const size_t unit = at / base_bits, unit_at = at % base_bits;
		if (unit <= fixed_len-1)
			fixed_ptr[unit] ^= (1 << unit_at);
	}

	void arbit::clear_first_bits(size_t before)
	{
		const size_t unit = before / base_bits, unit_at = before % base_bits;
		for (ssize_t i = unit; i >= 0; i--)
		{
			if ((size_t)i == unit)
			{
				fixed_ptr[i] >>= unit_at;
				fixed_ptr[i] <<= unit_at;
			}
			else
				fixed_ptr[i] = 0;
		}
	}

	void arbit::zero()
	{
		if (fixed_len > 1)
			shrink(fixed_len - 1);
		else if (fixed_len == 0)
			grow(1);
		fixed_ptr[0] = 0;
	}

	bool arbit::is_zero() const
	{
		for (size_t i=0; i < fixed_len; i++)
			if (fixed_ptr[i] != 0)
				return false;
		return true;
	}

	bool arbit::is_negative() const
	{
		if (fixed_len > 0)
			return fixed_ptr[fixed_len-1] >> (base_bits - 1);
		return false;
	}

	bool arbit::is_negative(base_t n)
	{
		return n >> (base_bits - 1);
	}

	void arbit::shrink_if_can()
	{
		const auto neg = is_negative();
		const auto check = neg ? base_max : 0;

		ssize_t i = (ssize_t)fixed_len - 1;
		for (; i >= 1; i--)
		{
			if (fixed_ptr[i] == check)
			{
				if (is_negative(fixed_ptr[i-1]) == neg)
					continue;
				else break;
			} else break;
		}

		const ssize_t by = (ssize_t)fixed_len - 1 - i;
		if (by > 0)
			shrink(by);
	}

	size_t arbit::bytes() const
	{
		return fixed_len * sizeof(base_t);
	}

	arbit& arbit::negate()
	{
		for (size_t i=0; i < fixed_len; i++)
			fixed_ptr[i] = ~fixed_ptr[i];
		*this += 1;
		return *this;
	}

	arbit& arbit::operator+=(const arbit& rhs)
	{
		if (rhs.fixed_len == 0)
			return *this;

		if (fixed_len < rhs.fixed_len)
		{
			const size_t by = rhs.fixed_len - fixed_len;
			grow(by);
		}

		const auto neg = is_negative(), neg_rhs = rhs.is_negative();

		base_double_t carry = 0;
		for (size_t i=0; i < fixed_len; i++)
		{
			const base_t unit = fixed_ptr[i];
			const base_t unit_rhs = i < rhs.fixed_len ? rhs.fixed_ptr[i] : 0;
			
			const base_double_t sum = base_double_t(unit) + base_double_t(unit_rhs) + carry;
			carry = sum >> base_bits;

			fixed_ptr[i] = sum;

			if (i == fixed_len-1)
			{
				if (neg && neg_rhs && !is_negative(sum))
					grow(1, true);
				else if (!neg && !neg_rhs && is_negative(sum))
					grow(1, false);
				break;
			}
		}

		return *this;
	}

	arbit& arbit::operator*=(const arbit& rhs)
	{
		if (rhs.is_zero())
		{
			zero();
			return *this;
		}

		arbit copy(*this), copy_rhs(rhs);

		const sbase_t change = rhs.is_negative() ? 1 : -1;
		copy_rhs += change;

		while (!copy_rhs.is_zero())
		{
			*this += copy;
			copy_rhs += change;
		}

		if (rhs.is_negative())
			negate();

		return *this;
	}

	arbit arbit::operator*(const arbit& rhs) const
	{
		arbit product(0);

		if (fixed_len == 0 || rhs.fixed_len == 0)
			return product;

		arbit copy(*this), copy_rhs(rhs);

		const size_t total_len = std::max(fixed_len, rhs.fixed_len) * 2;
		if (product.fixed_len < total_len)
			product.grow(total_len - product.fixed_len);
		if (copy.fixed_len < total_len)
			copy.grow(total_len - copy.fixed_len);
		if (copy_rhs.fixed_len < total_len)
			copy_rhs.grow(total_len - copy_rhs.fixed_len);

		const auto bits = copy_rhs.bytes() * 8;
		for (size_t i=0; i < bits; i++)
		{
			if (copy_rhs.bit(i))
				product += copy;

			copy <<= 1;
		}

		if (product.fixed_len > total_len)
			product.shrink(product.fixed_len - total_len);

		product.shrink_if_can();
		return product;

		/*
		arbit product(0), copy_rhs(rhs);

		const sbase_t change = rhs.is_negative() ? 1 : -1;

		while (!copy_rhs.is_zero())
		{
			product += *this;
			copy_rhs += change;
		}

		if (rhs.is_negative())
			product.negate();

		return product;
		*/
	}

	arbit& arbit::operator+=(arbit::sbase_t rhs)
	{
		if (fixed_len == 0)
			grow(1);

		const auto neg = is_negative(), neg_rhs = rhs < 0;

		base_double_t carry = 0;
		for (size_t i=0; i < fixed_len; i++)
		{
			const base_t unit = fixed_ptr[i];
			const base_t unit_rhs = i == 0 ? rhs : 0;
			
			const base_double_t sum = base_double_t(unit) + base_double_t(unit_rhs) + carry;
			carry = sum >> base_bits;

			fixed_ptr[i] = sum;

			if (i == fixed_len-1)
			{
				if (neg && neg_rhs && !is_negative(sum))
					grow(1, true);
				else if (!neg && !neg_rhs && is_negative(sum))
					grow(1, false);
				break;
			}
		}

		return *this;
	}

	arbit& arbit::operator*=(arbit::sbase_t rhs)
	{
		if (rhs == 0)
		{
			zero();
			return *this;
		}

		arbit copy(*this);

		const auto neg_rhs = rhs < 0;

		const sbase_t change = neg_rhs ? 1 : -1;
		rhs += change;

		while (rhs != 0)
		{
			*this += copy;
			rhs += change;
		}

		if (neg_rhs)
			negate();

		return *this;
	}

	arbit& arbit::operator<<=(size_t by)
	{
		const auto bits = bytes() * 8;
		if (by >= bits)
		{
			memset(fixed_ptr, 0, bytes());
		}
		else if (by > 0)
		{
			for (ssize_t i = (ssize_t)bits-1; i >= 0; i--)
			{
				if ((size_t)i >= by)
				{
					const size_t at = (size_t)i - by;
					if (bit(at)) set_bit(i);
					else clear_bit(i);
				}
				else break;
			}
			const auto upto = by;
			clear_first_bits(upto);
		}

		return *this;
	}

	arbit& arbit::operator=(const arbit& rhs)
	{
		if (fixed_ptr) {
			internal_free(fixed_ptr);
			fixed_ptr = nullptr;
		}
		if (decimal_ptr) {
			internal_free(decimal_ptr);
			decimal_ptr = nullptr;
		}

		precision = rhs.precision;
		fixed_len = decimal_len = 0;
		actual_fixed_len = actual_decimal_len = 0;

		grow(rhs.fixed_len);

		if (fixed_len > 0)
			memcpy(fixed_ptr, rhs.fixed_ptr, sizeof(base_t) * fixed_len);
		if (decimal_len > 0)
			memcpy(decimal_ptr, rhs.decimal_ptr, sizeof(base_t) * decimal_len);

		return *this;
	}

	void arbit::raw_print(char way, bool newline) const
	{
		std::print("({},{})", actual_fixed_len, fixed_len);
		if (fixed_len > 0)
		{
			std::print(" ");
			if (is_negative()) std::print("!");
			for (unsigned i=0; i < fixed_len; i++)
			{
				auto unit = fixed_ptr[i];
				if (way == 'b')
					std::print("{:#b}", unit);
				else if (way == 'x')
					std::print("{:#x}", unit);
				else if (way == 's')
					std::print("{}", sbase_t(unit));
				else
					std::print("{}", unit);
				if (i != fixed_len-1)
					std::print(" ");
			}
			if (newline)
				std::println("");
		}

		if (decimal_len > 0)
		{
			WC_STD_EXCEPTION("{}:{}: Decimal printing unimplemented", __FILE__, __LINE__);
		}
	}

	void arbit::print() const
	{
		/*
		std::string digits;

		const bool neg = is_negative();

		{
			arbit copy;
			const arbit* source = this;
			if (neg)
			{
				source = &copy;
				copy = *this;
				copy.negate();
			}

			for (size_t i=0; i < source->fixed_len; i++)
			{
				base_t n = source->fixed_ptr[i];
				while (n != 0)
				{
					digits += std::to_string(n % 10);
					n /= 10;
				}
			}
		}

		if (digits.empty()) digits += "0";
		std::reverse(digits.begin(), digits.end());

		if (neg) std::print("-");
		std::print("{}", digits);

		if (decimal_len > 0)
			WC_STD_EXCEPTION("{}:{}: Decimal printing is broken", __FILE__, __LINE__);
		*/
	}

	/* private members */

	void arbit::parse(std::string_view both)
	{
		if (both.size() == 0)
			WC_ARBIT_EXCEPTION(parse, "Empty string");

		bool is_decimal = false;
		std::string_view::iterator fixed_end = both.end();

		bool neg = false;
		auto it = both.begin();
		if (both[0] == '-')
		{
			neg = true;
			it = both.begin()+1;
		}

		for (; it != both.end(); it++)
		{
			char c = *it;
			if (c == '.')
			{
				if (is_decimal)
					WC_ARBIT_EXCEPTION(parse, "Encountered a second decimal point in '{}'", both);
				if (it != both.end()-1)
					is_decimal = true;
				fixed_end = it;
				continue;
			}
			else if (!isdigit(c))
			{
				WC_ARBIT_EXCEPTION(parse, "'{}' is not a digit in '{}'", c, both);
			}
		}

		std::string_view fixed(both.begin(), fixed_end), decimal;
		if (is_decimal)
			decimal = std::string_view(std::next(fixed_end), both.end());
		parse(fixed, decimal, neg);
	}

	void arbit::parse(std::string_view fixed, std::string_view decimal, bool neg)
	{
		arbit multiplier(1);

		for (auto it = fixed.rbegin(); it != fixed.rend(); it++)
		{
			arbit cur((*it) - '0');
			cur *= multiplier;
			multiplier *= 10;

			*this += cur;
		}

		if (neg)
			negate();

		if (decimal.size() > 0)
		{
			WC_STD_EXCEPTION("{}:{}: Decimal parsing is broken", __FILE__, __LINE__);
		}
	}

	void arbit::grow(size_t by)
	{
		grow(by, is_negative());
	}

	void arbit::grow(size_t by, bool neg)
	{
		const auto has_len = actual_fixed_len - fixed_len;
		if (by <= has_len)
		{
			memset(&fixed_ptr[fixed_len], neg ? 0xff : 0, sizeof(base_t) * by);
			fixed_len += by;
		}
		else
		{
			const size_t grow_const = 1, grow_upper_limit = 1000;
			const auto new_actual_fixed_len = actual_fixed_len + by + grow_const;
			const auto new_fixed_len = fixed_len + by;

			if (new_actual_fixed_len > grow_upper_limit)
				WC_STD_EXCEPTION("Cannot grow to {} as {} is the upper limit",
								 new_actual_fixed_len, grow_upper_limit);

			if (!(fixed_ptr = (base_t*)internal_realloc(fixed_ptr, sizeof(base_t) * new_actual_fixed_len)))
				WC_STD_EXCEPTION("Failed to reallocate from length {} to {}",
								 actual_fixed_len, new_actual_fixed_len);

			memset(&fixed_ptr[fixed_len], neg ? 0xff : 0, sizeof(base_t) * by);

			actual_fixed_len = new_actual_fixed_len;
			fixed_len = new_fixed_len;
		}
	}

	void arbit::shrink(size_t by)
	{
		if (by >= fixed_len)
			WC_STD_EXCEPTION("Cannot shrink by {} when {} is all it has", by, fixed_len);

		const size_t new_actual_fixed_len = actual_fixed_len - by;
		const size_t new_fixed_len = fixed_len - by;

		if(!(fixed_ptr = (base_t*)internal_realloc(fixed_ptr, sizeof(base_t) * new_actual_fixed_len)))
			WC_STD_EXCEPTION("Failed to reallocate from length {} to {}",
							 actual_fixed_len, new_actual_fixed_len);

		actual_fixed_len = new_actual_fixed_len;
		fixed_len = new_fixed_len;
	}

	// Heap stuff
	
	void* arbit::internal_malloc(size_t size)
	{
		auto ptr = malloc(size);
		if (!ptr)
			return nullptr;

		heap_allocations[ptr] = size;

		heap_mallocs++;
		heap_current += size;
		if (heap_current > heap_max)
			heap_max = heap_current;
		if (heap_allocations.size() > heap_max_entries)
			heap_max_entries = heap_allocations.size();

		return ptr;
	}

	void* arbit::internal_realloc(void* ptr, size_t new_size)
	{
		auto new_ptr = realloc(ptr, new_size);
		if (!new_ptr)
			return nullptr;

		heap_current += new_size;

		if (ptr == nullptr)
		{
			heap_mallocs++;
			heap_allocations[new_ptr] = new_size;
		}
		else
		{
			auto it = heap_allocations.find(ptr);

			heap_reallocs++;
			heap_current -= it->second;

			if (ptr == new_ptr)
			{
				it->second = new_size;
			}
			else
			{
				heap_allocations.erase(it);
				heap_allocations[new_ptr] = new_size;
			}
		}

		if (heap_current > heap_max)
			heap_max = heap_current;
		if (heap_allocations.size() > heap_max_entries)
			heap_max_entries = heap_allocations.size();

		return new_ptr;
	}

	void arbit::internal_free(void* ptr)
	{
		free(ptr);

		auto it = heap_allocations.find(ptr);
		if (it == heap_allocations.end())
		{
			WC_STD_EXCEPTION("Free called on a non-existent heap pointer {}", (size_t)ptr);
		}
		else
		{
			heap_frees++;
			heap_current -= it->second;
			heap_allocations.erase(it);
		}
	}
};
