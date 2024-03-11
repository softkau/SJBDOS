#pragma once

#include <type_traits>
#include <array>

template <typename T>
struct ArrayLength {}; // primary template

// template specialization for c-array
template <typename T, size_t N>
struct ArrayLength<T[N]> {
	static const size_t value = N;
};

// template specialization for std-array
template <typename T, size_t N>
struct ArrayLength<std::array<T, N>> {
	static const size_t value = N;
};

template <class T>
class MemMapRegister {
public:
	T Read() const {
		T tmp;
		for (size_t i = 0; i < len; i++)
			tmp.data[i] = value.data[i];
		return tmp;
	}

	void Write(const T& value) {
		for (size_t i = 0; i < len; i++)
			this->value.data[i] = value.data[i];
	}

private:
	volatile T value;
	static constexpr size_t len = ArrayLength<decltype(T::data)>::value;
};

template <class T>
struct DefaultBitmap {
	T data[1];

	DefaultBitmap& operator=(const T& value) {
		data[0] = value;
		return *this;
	}
	operator T() const { return data[0]; }
};

template <typename T>
struct ArrayWrapper {
	using ValueType = T;
	using Iterator = ValueType*;
	using ConstIterator = const ValueType*;

	ArrayWrapper(uintptr_t array_base_addr, size_t size)
		: c(reinterpret_cast<ValueType*>(array_base_addr)),
		  size(size) {}

	size_t Size() const { return size; }

	Iterator begin() { return c; }
	Iterator end() { return c + size; }
	ConstIterator cbegin() const { return c; }
	ConstIterator cend() const { return c + size; }

	ValueType& operator[](size_t index) { return c[index]; }
	const ValueType& operator[](size_t index) const { return c[index]; }

private:
	ValueType* const c;
	const size_t size;
};