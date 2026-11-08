#ifndef APP_CONTRACTS_MONEY_HPP
#define APP_CONTRACTS_MONEY_HPP

#include <cstdint>
#include <string>
#include <ostream>
#include "app/contracts/result.hpp"

namespace app::contracts {

class Money {
public:
    constexpr Money() : cents_(0) {}
    constexpr explicit Money(std::int64_t cents) : cents_(cents) {}

    static constexpr Money from_cents(std::int64_t cents) {
        return Money(cents);
    }

    static constexpr Money from_major_minor(std::int64_t pesos, std::int64_t cents) {
        return Money(pesos * 100 + cents);
    }

    static constexpr Money zero() {
        return Money(0);
    }

    constexpr std::int64_t cents() const { return cents_; }

    std::string to_string() const;
    static Result<Money> try_parse(const std::string& str, bool allow_negative = false);
    static Money from_string(const std::string& str);

    constexpr Money operator+(const Money& other) const {
        return Money(cents_ + other.cents_);
    }

    constexpr Money operator-(const Money& other) const {
        return Money(cents_ - other.cents_);
    }

    constexpr Money operator*(std::int64_t factor) const {
        return Money(cents_ * factor);
    }

    Money& operator+=(const Money& other) {
        cents_ += other.cents_;
        return *this;
    }

    Money& operator-=(const Money& other) {
        cents_ -= other.cents_;
        return *this;
    }

    Money& operator*=(std::int64_t factor) {
        cents_ *= factor;
        return *this;
    }

    constexpr bool operator==(const Money& other) const { return cents_ == other.cents_; }
    constexpr bool operator!=(const Money& other) const { return cents_ != other.cents_; }
    constexpr bool operator<(const Money& other) const { return cents_ < other.cents_; }
    constexpr bool operator<=(const Money& other) const { return cents_ <= other.cents_; }
    constexpr bool operator>(const Money& other) const { return cents_ > other.cents_; }
    constexpr bool operator>=(const Money& other) const { return cents_ >= other.cents_; }

    friend std::ostream& operator<<(std::ostream& os, const Money& m) {
        return os << m.to_string();
    }

private:
    std::int64_t cents_;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_MONEY_HPP
