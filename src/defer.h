#pragma once

namespace detail {

template
<typename F>
class scope_guard {
public:
	scope_guard(F f) : func(std::move(f)), present(true) {
	}

	~scope_guard() {
		if (present) {
			func();
		}
	}

	scope_guard() = delete;
	scope_guard(const scope_guard&) = delete;
	scope_guard& operator=(const scope_guard&) = delete;

	scope_guard(scope_guard&& rhs) : func(std::move(rhs.func)), present(rhs.present) {
		rhs.present = false;
	}

private:
	F func;
	bool present;
};

enum class scope_guard_helper {
};

template<typename F>
scope_guard<F> operator+(scope_guard_helper, F&& fn) {
	return scope_guard<F>(std::forward<F>(fn));
}

} // namespace detail

#define DEFER const auto _DEFER_##__LINE__ = ::detail::scope_guard_helper() + [&]()
