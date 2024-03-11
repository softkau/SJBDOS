#pragma once
#include <cstddef>
#include <array>
#include "error.hpp"
#include "interrupt.hpp"

template <typename T, size_t N>
struct ArrayQueue {
	Error push(const T& x) {
		if (size() == capacity()) return MakeError(Error::kFull);
		c[rear++] = x; sz++;
		if (rear == capacity()) rear = 0;
		return MakeError(Error::kSuccess);
	}
	Error pop() {
		if (empty()) return MakeError(Error::kEmpty);
		head++; sz--;
		if (head == capacity()) head = 0;
		return MakeError(Error::kSuccess);
	}
	T& front() {
		return c[head];
	}
	const T& front() const {
		return c[head];
	}
	constexpr size_t capacity() const { return N; }
	size_t size() const { return sz; }
	bool empty() const { return !this->size(); }

private:
	std::array<T, N> c;
	size_t sz = 0;
	size_t head = 0;
	size_t rear = 0;
};

// T가 동적 할당된 경우 POP 시 container 내부에 존재하기 때문에 유의
template <typename T, size_t N>
struct ArrayPriorityQueue {
	T& top() { return c[1]; }
	const T& top() const { return c[1]; }
	void push(const T& x) {
		c[++sz] = x;
		size_t i = sz;
		size_t p = sz >> 1;
		while (p && c[p] < c[i]) {
			T tmp = c[p];
			c[p] = c[i];
			c[i] = tmp;
			i = p;
			p >>= 1;
		}
	}

	void pop() {
		c[1] = c[sz--];

		// heapify
		size_t i = 1;
		while (i < sz) {
			size_t l = (i << 1);
			size_t r = (i << 1) + 1;
			bool l_res = l <= sz && c[i] < c[l];
			bool r_res = r <= sz && c[i] < c[r];
			if (!l_res && !r_res) break;

			if (r <= sz && c[l] < c[r]) {
				T tmp = c[i];
				c[i] = c[r];
				c[r] = tmp;
			}
			else {
				T tmp = c[i];
				c[i] = c[l];
				c[l] = tmp;
			}
			i <<= 1;
		}
	}

	size_t capacity() const {
		return N;
	}
	size_t size() const {
		return sz;
	}
	bool empty() const {
		return sz == 0;
	}

private:
	std::array<T, N+1> c;
	size_t sz = 0;
};