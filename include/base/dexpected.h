// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef DEXPECTED_H
#define DEXPECTED_H

#include "dtkcore_global.h"
#include <cassert>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <memory>
#include <type_traits>

DCORE_BEGIN_NAMESPACE

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#if __cpp_exceptions
#    define _DEXPECTED_THROW_OR_ABORT(_EXC) (throw(_EXC))
#else
#    define _DEXPECTED_THROW_OR_ABORT(_EXC) (std::abort())
#endif

template <typename T, typename E>
class Dexpected;

template <typename E>
class Dunexpected;

template <bool v>
using _bool_constant = std::integral_constant<bool, v>;

/*!
 * @brief 原位构造标签
 */
enum class emplace_tag { USE_EMPLACE /**< 使用原位构造 */ };

/*!
 * @brief 从Dunexpected构造标签
 */
enum class dunexpected_tag { DUNEXPECTED /**< 从Dunexpected构造 */ };

template <typename Type>
struct remove_cvref
{
    using type = typename std::remove_cv<typename std::remove_reference<Type>::type>::type;
};

template <typename E>
class bad_result_access;

template <>
class bad_result_access<void> : public std::exception
{
protected:
    bad_result_access() noexcept {}
    bad_result_access(const bad_result_access &) = default;
    bad_result_access(bad_result_access &&) = default;
    bad_result_access &operator=(const bad_result_access &) = default;
    bad_result_access &operator=(bad_result_access &&) = default;
    ~bad_result_access() = default;

public:
    const char *what() const noexcept override { return "bad access to Dexpected without value"; }
};

template <typename E>
class bad_result_access : public bad_result_access<void>
{
public:
    explicit bad_result_access(E _e)
        : m_error(std::move(_e))
    {
    }

    E &error() &noexcept { return m_error; }
    const E &error() const &noexcept { return m_error; }

    E error() &&noexcept { return std::move(m_error); }
    const E error() const &&noexcept { return std::move(m_error); }

private:
    E m_error;
};

template <typename Type, typename... Args>
auto construct_at(Type *p, Args &&...args) noexcept(noexcept(::new((void *)0) Type(std::declval<Args>()...)))
    -> decltype(::new((void *)0) Type(std::declval<Args>()...))
{
    return ::new ((void *)p) Type(std::forward<Args>(args)...);
}

namespace __dexpected {
template <typename ObjType>
void destroy_at_obj(ObjType *p)
{
    p->~ObjType();
}

template <typename ArrType>
void destroy_at_arr(ArrType *p)
{
    for (auto &elem : *p)
        destroy_at_obj(std::addressof(elem));
}
}  // namespace __dexpected

template <typename Type, typename std::enable_if<std::is_array<Type>::value, bool>::type = true>
void destroy_at(Type *p)
{
    __dexpected::destroy_at_arr(p);
}

template <typename Type, typename std::enable_if<!std::is_array<Type>::value, bool>::type = true>
void destroy_at(Type *p)
{
    __dexpected::destroy_at_obj(p);
}

namespace __dexpected {
template <typename T>
struct Guard
{
    static_assert(std::is_nothrow_move_constructible<T>::value, "type T must bu nothrow_move_constructible");
    explicit Guard(T &_x)
        : m_guarded(std::addressof(_x))
        , m_tmp(std::move(_x))
    {
        destroy_at(m_guarded);
    }

    ~Guard()
    {
        if (unlikely(m_guarded)) {
            construct_at(m_guarded, std::move(m_tmp));
        }
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

    T &&release() noexcept { return std::move(m_tmp); }

private:
    T *m_guarded;
    T m_tmp;
};
}  // namespace __dexpected

namespace __dexpected {

template <typename T>
struct _is_dexpected : public std::false_type
{
};

template <typename T, typename E>
struct _is_dexpected<Dexpected<T, E>> : public std::true_type
{
};

template <typename T>
struct _is_dunexpected : public std::false_type
{
};

template <typename T>
struct _is_dunexpected<Dunexpected<T>> : public std::true_type
{
};

template <typename E>
constexpr bool _can_be_dunexpected()
{
    return std::is_object<E>::value and !std::is_array<E>::value and !_is_dunexpected<E>() and !std::is_const<E>::value and
           !std::is_volatile<E>::value;
}

template <typename Tp,
          typename Up,
          typename Vp,
          typename std::enable_if<std::is_nothrow_constructible<Tp, Vp>::value and !std::is_nothrow_move_constructible<Tp>::value,
                                  bool>::type = true>
void reinit(Tp *_newVal, Up *_oldVal, Vp &&_arg) noexcept(std::is_nothrow_constructible<Tp, Vp>::value)
{
    destroy_at(_oldVal);
    construct_at(_newVal, std::forward<Vp>(_arg));
}

template <typename Tp,
          typename Up,
          typename Vp,
          typename std::enable_if<!std::is_nothrow_constructible<Tp, Vp>::value and std::is_nothrow_move_constructible<Tp>::value,
                                  bool>::type = true>
void reinit(Tp *_newVal, Up *_oldVal, Vp &&_arg) noexcept(std::is_nothrow_constructible<Tp, Vp>::value)
{
    Tp _tmp(std::forward<Vp>(_arg));
    destroy_at(_oldVal);
    construct_at(_newVal, std::move(_tmp));
}

template <
    typename Tp,
    typename Up,
    typename Vp,
    typename std::enable_if<!std::is_nothrow_constructible<Tp, Vp>::value and !std::is_nothrow_move_constructible<Tp>::value,
                            bool>::type = true>
void reinit(Tp *_newVal, Up *_oldVal, Vp &&_arg) noexcept(std::is_nothrow_constructible<Tp, Vp>::value)
{
    __dexpected::Guard<Up> _guard(*_oldVal);
    construct_at(_newVal, std::forward<Vp>(_arg));
    _guard.release();
}

}  // namespace __dexpected

/*!
 * @brief 类模板Dtk::Core::Dunexpected代表一个Dtk::Core::Dexpected中存储的不期待的值
 * @tparam E 不期待的值的类型，该类型不能是非对象类型，数组类型，Dtk::Core::Dunexpected的特化类型或有cv限定符的类型
 */
template <typename E>
class Dunexpected
{
    static_assert(__dexpected::_can_be_dunexpected<E>(), "can't be dunexpected");

public:
    /*!
     * @brief Dtk::Core::Dunexpected的默认拷贝构造函数
     */
    constexpr Dunexpected(const Dunexpected &) = default;

    /*!
     * @brief Dtk::Core::Dunexpected的默认移动构造函数
     */
    constexpr Dunexpected(Dunexpected &&) = default;

    /*!
     * @brief 使用类型E直接初始化一个Dtk::Core::Dunexpected对象
     * @tparam Er 错误类型，默认为E
     * @param[in] _e 一个类型为Er的值
     */
    template <typename Er = E,
              typename std::enable_if<!std::is_same<typename remove_cvref<Er>::type, Dunexpected>::value and
                                          !std::is_same<typename remove_cvref<Er>::type, emplace_tag>::value and
                                          std::is_constructible<E, Er>::value,
                                      bool>::type = true>
    constexpr explicit Dunexpected(Er &&_e) noexcept(std::is_nothrow_constructible<E, Er>::value)
        : m_error(std::forward<Er>(_e))
    {
    }

    /*!
     * @brief 直接从参数构造出一个包含错误类型为E的对象的Dtk::Core::Dunexpected对象
     * @tparam Args 可变参数模板类型，这里是构造类型为E的对象所需要的参数的类型
     * @param[in] args 构造类型为E的对象用到的参数
     * @attention 为了区分是构造E还是Dtk::Core::Dunexpected，需要在第一个参数使用emplace_tag进行标识
     */
    template <typename... Args>
    constexpr explicit Dunexpected(emplace_tag, Args &&...args) noexcept(std::is_nothrow_constructible<E, Args...>::value)
        : m_error(std::forward<Args>(args)...)
    {
        static_assert(std::is_constructible<E, Args...>::value, "can't construct E from args.");
    }

    /*!
     * @brief 从参数和初始化列表构造出一个包含错误类型为E的对象的Dtk::Core::Dunexpected对象
     * @tparam U 初始化列表的模板类型
     * @tparam Args 可变参数模板类型，这里是构造类型为E的对象所需要的参数的类型
     * @param _li 模板类型为U的初始化列表
     * @param[in] args 构造类型为E的对象用到的参数
     * @attention 为了区分是构造E还是Dtk::Core::Dunexpected，需要在第一个参数使用emplace_tag进行标识
     */
    template <typename U, typename... Args>
    constexpr explicit Dunexpected(emplace_tag, std::initializer_list<U> _li, Args &&...args) noexcept(
        std::is_nothrow_constructible<E, std::initializer_list<U> &, Args...>::value)
        : m_error(_li, std::forward<Args>(args)...)
    {
    }

    /*!
     * @brief Dtk::Core::Dunexpected的默认拷贝赋值运算符
     */
    Dunexpected &operator=(const Dunexpected &) = default;

    /*!
     * @brief Dtk::Core::Dunexpected的默认移动赋值运算符
     */
    Dunexpected &operator=(Dunexpected &&) = default;

    /*!
     * @brief 获取Dtk::Core::Dunexpected持有的不期待值
     * @return 不期待值的const左值引用
     */
    constexpr const E &error() const &noexcept { return m_error; }

    /*!
     * @brief 获取Dtk::Core::Dunexpected持有的不期待值
     * @return 不期待值的左值引用
     */
    E &error() &noexcept { return m_error; }

    /*!
     * @brief 获取Dtk::Core::Dunexpected持有的不期待值
     * @attention 获取后原Dtk::Core::Dunexpected不可用
     * @return 不期待值的const右值引用
     */
    constexpr const E &&error() const &&noexcept { return std::move(m_error); }

    /*!
     * @brief 获取Dtk::Core::Dunexpected持有的不期待值
     * @attention 获取后原Dtk::Core::Dunexpected不可用
     * @return 不期待值的右值引用
     */
    E &&error() &&noexcept { return std::move(m_error); }

    /*!
     * @brief 交换两个Dtk::Core::Dunexpected的值
     * @param[in] _other 另一个模板参数为E的Dtk::Core::Dunexpected对象
     */
    void swap(Dunexpected &_other)
    {
        using std::swap;
        swap(m_error, _other.m_error);
    }

    /*!
     * @brief 重载相等运算符
     * @tparam Er 另一个Dtk::Core::Dunexpected的模板类型
     * @param[in] _x 模板参数为E的Dtk::Core::Dunexpected对象
     * @param[in] _y 模板参数为Er的Dtk::Core::Dunexpected对象
     */
    template <typename Er>
    friend constexpr bool operator==(const Dunexpected &_x, const Dunexpected<Er> _y)
    {
        return _x.m_error == _y.error();
    }

    /*!
     * @brief 交换两个Dtk::Core::Dunexpected的值
     */
    friend void swap(Dunexpected &_x, Dunexpected &_y) { _x.swap(_y); }

private:
    E m_error;
};

/*!
 * @brief
 * 模板类Dtk::Core::Dexpected提供存储两个值之一的方式。Dtk::Core::Dexpected的对象要么保有一个期待的T类型值，要么保有一个不期待的E类型值，不会没有值。
 * @tparam T 期待的类型
 * @tparam E 不期待的类型
 */
template <typename T, typename E>
class Dexpected
{
    template <typename, typename>
    friend class Dexpected;
    static_assert(!std::is_reference<T>::value, "type T can't be reference type");
    static_assert(!std::is_function<T>::value, "type T can't be function type");
    static_assert(!std::is_same<typename std::remove_cv<T>::type, dunexpected_tag>::value, "type T can't be dunexpected_tag");
    static_assert(!std::is_same<typename std::remove_cv<T>::type, emplace_tag>::value, "type T can't be emplace_tag");
    static_assert(!__dexpected::_is_dunexpected<typename std::remove_cv<T>::type>::value, "type T can't be Dunexpected");
    static_assert(__dexpected::_can_be_dunexpected<E>(), "type E can't be dunexpected");

    template <typename U, typename G, typename Unex = Dunexpected<E>>
    static constexpr bool __cons_from_Dexpected()
    {
        return std::is_constructible<T, Dexpected<U, G> &>::value or std::is_constructible<T, Dexpected<U, G>>::value or
               std::is_constructible<T, const Dexpected<U, G>>::value or
               std::is_constructible<T, const Dexpected<U, G> &>::value or std::is_convertible<Dexpected<U, G> &, T>::value or
               std::is_convertible<Dexpected<U, G>, T>::value or std::is_convertible<const Dexpected<U, G> &, T>::value or
               std::is_convertible<const Dexpected<U, G>, T>::value or std::is_constructible<Unex, Dexpected<U, G> &>::value or
               std::is_constructible<Unex, Dexpected<U, G>>::value or
               std::is_constructible<Unex, const Dexpected<U, G> &>::value or
               std::is_constructible<Unex, const Dexpected<U, G>>::value;
    }

    template <typename U, typename G>
    static constexpr bool __explicit_conv()
    {
        return !std::is_convertible<U, T>::value or !std::is_convertible<G, E>::value;
    }

    static constexpr bool des_value()
    {
        return !std::is_trivially_destructible<T>::value or !std::is_trivially_destructible<E>::value;
    }

    template <typename V>
    void assign_val(V &&_v)
    {
        if (m_has_value) {
            m_value = std::forward<V>(_v);
        } else {
            __dexpected::reinit(std::addressof(m_value), std::addressof(m_error), std::forward<V>(_v));
            m_has_value = true;
        }
    }

    template <typename V>
    void assign_err(V &&_v)
    {
        if (m_has_value) {
            __dexpected::reinit(std::addressof(m_value), std::addressof(m_error), std::forward<V>(_v));
            m_has_value = false;
        } else {
            m_error = std::forward<V>(_v);
        }
    }

    template <typename Ep = E, typename std::enable_if<std::is_nothrow_move_constructible<Ep>::value, bool>::type = true>
    void swap_val_err(Dexpected &_other) noexcept(
        std::is_nothrow_move_constructible<Ep>::value and std::is_nothrow_move_constructible<T>::value)
    {
        __dexpected::Guard<E> _guard(_other.m_error);
        construct_at(std::addressof(_other.m_value), std::move(m_value));
        _other.m_has_value = true;
        destroy_at(std::addressof(m_value));
        construct_at(std::addressof(m_error), _guard.release());
        m_has_value = false;
    }

    template <typename Ep = E, typename std::enable_if<!std::is_nothrow_move_constructible<Ep>::value, bool>::type = true>
    void swap_val_err(Dexpected &_other) noexcept(
        std::is_nothrow_move_constructible<Ep>::value and std::is_nothrow_move_constructible<T>::value)
    {
        __dexpected::Guard<T> _guard(_other.m_value);
        construct_at(std::addressof(m_error), std::move(_other.m_error));
        m_has_value = false;
        destroy_at(std::addressof(_other.m_error));
        construct_at(std::addressof(_other.m_value), _guard.release());
        _other.m_has_value = true;
    }

public:
    using value_type = T;
    using error_type = E;
    using dunexpected_type = Dunexpected<E>;
    template <typename U>
    using rebind = Dexpected<U, error_type>;

    /*!
     * @brief Dtk::Core::Dexpected的默认构造函数
     */
    template <typename std::enable_if<std::is_default_constructible<T>::value, bool>::type = true>
    constexpr Dexpected() noexcept(std::is_nothrow_default_constructible<T>::value)
        : m_has_value(true)
        , m_value()
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的默认拷贝构造函数
     */
    Dexpected(const Dexpected &) = default;

    /*!
     * @brief Dtk::Core::Dexpected的拷贝构造函数
     */
    template <typename std::enable_if<std::is_copy_constructible<T>::value and std::is_copy_constructible<E>::value and
                                          (!std::is_trivially_copy_constructible<T>::value or
                                           !std::is_trivially_copy_constructible<E>::value),
                                      bool>::type = true>
    Dexpected(const Dexpected &_x) noexcept(
        std::is_nothrow_copy_constructible<T>::value and std::is_nothrow_copy_constructible<E>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()

    {
        if (m_has_value)
            construct_at(std::addressof(m_value), _x.m_value);
        else
            construct_at(std::addressof(m_error), _x.m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的默认移动构造函数
     */
    Dexpected(Dexpected &&) = default;

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数
     */
    template <typename std::enable_if<std::is_move_constructible<T>::value and std::is_move_constructible<E>::value and
                                          (!std::is_trivially_move_constructible<T>::value or
                                           !std::is_trivially_move_constructible<E>::value),
                                      bool>::type = true>
    Dexpected(Dexpected &&_x) noexcept(
        std::is_nothrow_move_constructible<T>::value and std::is_nothrow_move_constructible<E>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()
    {
        if (m_has_value)
            construct_at(std::addressof(m_value), std::move(_x).m_value);
        else
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的拷贝构造函数
     * @tparam U 另一个Dtk::Core::Dexpected的期待类型
     * @tparam G 另一个Dtk::Core::Dexpected的不期待类型
     * @param[in] _x 模板类型分别为U和G的Dtk::Core::Dexpected对象
     */
    template <
        typename U,
        typename G,
        typename std::enable_if<std::is_constructible<T, const U &>::value and std::is_constructible<E, const G &>::value and
                                    !__cons_from_Dexpected<U, G>() and !__explicit_conv<const U &, const G &>(),
                                bool>::type = true>
    Dexpected(const Dexpected<U, G> &_x) noexcept(
        std::is_nothrow_constructible<T, const U &>::value and std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()

    {
        if (m_has_value)
            construct_at(std::addressof(m_value), _x.m_value);
        else
            construct_at(std::addressof(m_error), _x.m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的拷贝构造函数
     * @tparam U 另一个Dtk::Core::Dexpected的期待类型
     * @tparam G 另一个Dtk::Core::Dexpected的不期待类型
     * @param[in] _x 模板类型分别为U和G的Dtk::Core::Dexpected对象
     * @attention 该拷贝构造函数有explicit标识
     */
    template <
        typename U,
        typename G,
        typename std::enable_if<std::is_constructible<T, const U &>::value and std::is_constructible<E, const G &>::value and
                                    !__cons_from_Dexpected<U, G>() and __explicit_conv<const U &, const G &>(),
                                bool>::type = true>
    explicit Dexpected(const Dexpected<U, G> &_x) noexcept(
        std::is_nothrow_constructible<T, const U &>::value and std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()
    {
        if (m_has_value)
            construct_at(std::addressof(m_value), _x.m_value);
        else
            construct_at(std::addressof(m_error), _x.m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数
     * @tparam U 另一个Dtk::Core::Dexpected的期待类型
     * @tparam G 另一个Dtk::Core::Dexpected的不期待类型
     * @param[in] _x 模板类型分别为U和G的Dtk::Core::Dexpected对象
     * @attention 构造后另一个Dtk::Core::Dexpected不可用
     */
    template <typename U,
              typename G,
              typename std::enable_if<std::is_constructible<T, U>::value and std::is_constructible<E, G>::value and
                                          !__cons_from_Dexpected<U, G>() and !__explicit_conv<U, G>(),
                                      bool>::type = true>
    Dexpected(Dexpected<U, G> &&_x) noexcept(
        std::is_nothrow_constructible<T, U>::value and std::is_nothrow_constructible<E, G>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()
    {
        if (m_has_value)
            construct_at(std::addressof(m_value), std::move(_x).m_value);
        else
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数
     * @tparam U 另一个Dtk::Core::Dexpected的期待类型
     * @tparam G 另一个Dtk::Core::Dexpected的不期待类型
     * @param[in] _x 模板类型分别为U和G的Dtk::Core::Dexpected对象
     * @attention 构造后另一个Dtk::Core::Dexpected不可用，该函数有explicit标识
     */
    template <typename U,
              typename G,
              typename std::enable_if<std::is_constructible<T, U>::value and std::is_constructible<E, G>::value and
                                          !__cons_from_Dexpected<U, G>() and __explicit_conv<U, G>(),
                                      bool>::type = true>
    explicit Dexpected(Dexpected<U, G> &&_x) noexcept(
        std::is_nothrow_constructible<T, U>::value and std::is_nothrow_constructible<E, G>::value)
        : m_has_value(_x.m_has_value)
        , m_invalid()
    {
        if (m_has_value)
            construct_at(std::addressof(m_value), std::move(_x).m_value);
        else
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数，直接从期待类型构造出Dtk::Core::Dexpected对象
     * @tparam U Dtk::Core::Dexpected的期待类型，默认为类型T
     * @param[in] _v 期待类型为U的对象
     * @attention 构造后原对象不可用，该函数有explicit标识
     */
    template <typename U = T,
              typename std::enable_if<!std::is_same<typename remove_cvref<U>::type, Dexpected>::value and
                                          !std::is_same<typename remove_cvref<U>::type, emplace_tag>::value and
                                          !__dexpected::_is_dunexpected<typename remove_cvref<U>::type>::value and
                                          std::is_constructible<T, U>::value and !std::is_convertible<U, T>::value,
                                      bool>::type = true>
    constexpr explicit Dexpected(U &&_v) noexcept(std::is_nothrow_constructible<T, U>::value)
        : m_has_value(true)
        , m_value(std::forward<U>(_v))

    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数，直接从期待类型构造出Dtk::Core::Dexpected对象
     * @tparam U Dtk::Core::Dexpected的期待类型，默认为类型T
     * @param[in] _v 期待类型为U的对象
     * @attention 构造后原对象不可用
     */
    template <typename U = T,
              typename std::enable_if<!std::is_same<typename remove_cvref<U>::type, Dexpected>::value and
                                          !std::is_same<typename remove_cvref<U>::type, emplace_tag>::value and
                                          !__dexpected::_is_dunexpected<typename remove_cvref<U>::type>::value and
                                          std::is_constructible<T, U>::value and std::is_convertible<U, T>::value,
                                      bool>::type = true>
    constexpr Dexpected(U &&_v) noexcept(std::is_nothrow_constructible<T, U>::value)
        : m_has_value(true)
        , m_value(std::forward<U>(_v))
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的拷贝构造函数，从Dtk::Core::Dunexpected构造出Dtk::Core::Dexpected对象
     * @tparam G Dtk::Core::Dexpected的期待类型，默认为类型E
     * @param[in] _u 期待类型为G的Dtk::Core::Dunexpected对象
     * @attention 该函数有explicit标识
     */
    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, const G &>::value and !std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    constexpr explicit Dexpected(const Dunexpected<G> &_u) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(false)
        , m_error(_u.error())
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的拷贝构造函数，从Dtk::Core::Dunexpected构造出Dtk::Core::Dexpected对象
     * @tparam G Dtk::Core::Dexpected的期待类型，默认为类型E
     * @param[in] _u 期待类型为G的Dtk::Core::Dunexpected对象
     */
    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, const G &>::value and std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    constexpr Dexpected(const Dunexpected<G> &_u) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(false)
        , m_error(_u.error())
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数，从Dtk::Core::Dunexpected构造出Dtk::Core::Dexpected对象
     * @tparam G Dtk::Core::Dexpected的期待类型，默认为类型E
     * @param[in] _u 期待类型为G的Dtk::Core::Dunexpected对象
     * @attention 构造后原对象不可用，该函数有explicit标识
     */
    template <
        typename G = E,
        typename std::enable_if<std::is_constructible<E, G>::value and !std::is_convertible<G, E>::value, bool>::type = true>
    constexpr explicit Dexpected(Dunexpected<G> &&_u) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(false)
        , m_error(std::move(_u).error())
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动构造函数，从Dtk::Core::Dunexpected构造出Dtk::Core::Dexpected对象
     * @tparam G Dtk::Core::Dexpected的期待类型，默认为类型E
     * @param[in] _u 期待类型为G的Dtk::Core::Dunexpected对象
     * @attention 构造后原对象不可用
     */
    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, G>::value and std::is_convertible<G, E>::value, bool>::type = true>
    constexpr Dexpected(Dunexpected<G> &&_u) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(false)
        , m_error(std::move(_u).error())
    {
    }

    /*!
     * @brief Dtk::Core::Dexpected的转发构造函数，从参数直接构造出期待值
     * @tparam Args 构造期待类型T所用到的参数的类型
     * @param[in] args 构造期待类型T所用到的参数
     * @attention 为了区分是构造T还是Dtk::Core::Dexpected，需要在第一个参数使用emplace_tag进行标识
     */
    template <typename... Args>
    constexpr explicit Dexpected(emplace_tag, Args &&...args) noexcept(std::is_nothrow_constructible<T, Args...>::value)
        : m_has_value(true)
        , m_value(std::forward<Args>(args)...)

    {
        static_assert(std::is_constructible<T, Args...>::value, "can't construct T from args.");
    }

    /*!
     * @brief Dtk::Core::Dexpected的转发构造函数，从参数直接构造出期待值
     * @tparam U 初始化列表的模板参数
     * @tparam Args 构造期待类型T所用到的参数的类型
     * @param[in] _li  构造期待类型T所用到的初始化列表
     * @param[in] args 构造期待类型T所用到的参数
     * @attention 为了区分是构造T还是Dtk::Core::Dexpected，需要在第一个参数使用emplace_tag进行标识
     */
    template <typename U, typename... Args>
    constexpr explicit Dexpected(emplace_tag, std::initializer_list<U> _li, Args &&...args) noexcept(
        std::is_nothrow_constructible<T, std::initializer_list<U> &, Args...>::value)
        : m_has_value(true)
        , m_value(_li, std::forward<Args>(args)...)
    {
        static_assert(std::is_constructible<T, std::initializer_list<U> &, Args...>::value, "can't construct T from args.");
    }

    /*!
     * @brief Dtk::Core::Dexpected的转发构造函数，从参数直接构造出不期待值
     * @tparam Args 构造不期待类型E所用到的参数的类型
     * @param[in] args 构造不期待类型E所用到的参数
     * @attention 为了区分是构造E还是Dtk::Core::Dexpected，需要在第一个参数使用dunexpected_tag进行标识
     */
    template <typename... Args>
    constexpr explicit Dexpected(dunexpected_tag, Args &&...args) noexcept(std::is_nothrow_constructible<E, Args...>::value)
        : m_has_value(false)
        , m_error(std::forward<Args>(args)...)

    {
        static_assert(std::is_constructible<E, Args...>::value, "can't construct E from args.");
    }

    /*!
     * @brief Dtk::Core::Dexpected的转发构造函数，从参数直接构造出不期待值
     * @tparam U 初始化列表的模板参数
     * @tparam Args 构造不期待类型E所用到的参数的类型
     * @param[in] _li  构造不期待类型E所用到的初始化列表
     * @param[in] args 构造不期待类型E所用到的参数
     * @attention 为了区分是构造E还是Dtk::Core::Dexpected，需要在第一个参数使用dunexpected_tag进行标识
     */
    template <typename U, typename... Args>
    constexpr explicit Dexpected(dunexpected_tag, std::initializer_list<U> _li, Args &&...args) noexcept(
        std::is_nothrow_constructible<E, std::initializer_list<U> &, Args...>::value)
        : m_has_value(false)
        , m_error(_li, std::forward<Args>(args)...)
    {
        static_assert(std::is_constructible<E, std::initializer_list<U> &, Args...>::value, "can't construct E from args.");
    }

    /*!
     * @brief Dtk::Core::Dexpected的析构函数
     */
    ~Dexpected()
    {
        if (des_value()) {
            if (m_has_value) {
                destroy_at(std::addressof(m_value));
            } else {
                destroy_at(std::addressof(m_error));
            }
        }
    }

    /*!
     * @brief Dtk::Core::Dexpected的默认拷贝赋值运算符
     */
    Dexpected &operator=(const Dexpected &) = delete;

    /*!
     * @brief Dtk::Core::Dexpected的拷贝赋值运算符
     * @param[in] _x 同类型的Dtk::Core::Dexpected对象
     */
    template <typename std::enable_if<std::is_copy_assignable<T>::value and std::is_copy_constructible<T>::value and
                                          std::is_copy_assignable<E>::value and std::is_copy_constructible<E>::value and
                                          (std::is_nothrow_move_constructible<T>::value or
                                           std::is_nothrow_move_constructible<E>::value),
                                      bool>::type = true>
    Dexpected &operator=(const Dexpected &_x) noexcept(
        std::is_nothrow_copy_constructible<T>::value and std::is_nothrow_copy_constructible<E>::value
            and std::is_nothrow_copy_assignable<T>::value and std::is_nothrow_copy_assignable<E>::value)
    {
        if (_x.m_has_value)
            this->assign_val(_x.m_value);
        else
            this->assign_err(_x.m_error);
        return *this;
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动赋值运算符
     * @param[in] _x 同类型的Dtk::Core::Dexpected对象
     * @attention 赋值后原对象不可用
     */
    template <typename std::enable_if<std::is_move_assignable<T>::value and std::is_move_constructible<T>::value and
                                          std::is_move_assignable<E>::value and std::is_move_constructible<E>::value and
                                          (std::is_nothrow_move_constructible<T>::value or
                                           std::is_nothrow_move_constructible<E>::value),
                                      bool>::type = true>
    Dexpected &operator=(Dexpected &&_x) noexcept(
        std::is_nothrow_move_constructible<T>::value and std::is_nothrow_move_constructible<E>::value
            and std::is_nothrow_move_assignable<T>::value and std::is_nothrow_move_assignable<E>::value)
    {
        if (_x.m_has_value)
            assign_val(std::move(_x.m_value));
        else
            assign_err(std::move(_x.m_error));
        return *this;
    }

    /*!
     * @brief Dtk::Core::Dexpected的转发赋值运算符
     * @tparam U 期待类型，默认为T
     * @param[in] _v 期待类型U的对象
     */
    template <
        typename U = T,
        typename std::enable_if<!std::is_same<Dexpected, typename remove_cvref<U>::type>::value and
                                    !__dexpected::_is_dunexpected<typename remove_cvref<U>::type>::value and
                                    std::is_constructible<T, U>::value and std::is_assignable<T &, U>::value and
                                    (std::is_nothrow_constructible<T, U>::value or std::is_nothrow_move_constructible<T>::value or
                                     std::is_nothrow_move_constructible<E>::value),
                                bool>::type = true>
    Dexpected &operator=(U &&_v)
    {
        assign_val(std::forward<U>(_v));
        return *this;
    }

    /*!
     * @brief Dtk::Core::Dexpected的拷贝赋值运算符
     * @tparam G 不期待类型
     * @param[in] _e 模板类型为G的Dtk::Core::Dunexpected对象
     */
    template <typename G,
              typename std::enable_if<std::is_constructible<E, const G &>::value and std::is_assignable<E &, const G &>::value and
                                          (std::is_nothrow_constructible<E, const G &>::value or
                                           std::is_nothrow_move_constructible<T>::value or std::is_move_constructible<E>::value),
                                      bool>::type = true>
    Dexpected &operator=(const Dunexpected<G> &_e)
    {
        assign_err(_e.error());
        return *this;
    }

    /*!
     * @brief Dtk::Core::Dexpected的移动赋值运算符
     * @tparam G 不期待类型
     * @param[in] _e 模板类型为G的Dtk::Core::Dunexpected对象
     * @attention 赋值后原对象不可用
     */
    template <typename G,
              typename std::enable_if<std::is_constructible<E, G>::value and std::is_assignable<E &, G>::value and
                                          (std::is_nothrow_constructible<E, G>::value or
                                           std::is_nothrow_move_constructible<T>::value or std::is_move_constructible<E>::value),
                                      bool>::type = true>
    Dexpected &operator=(Dunexpected<G> &&_e)
    {
        assign_err(std::move(_e).error());
        return *this;
    }

    /*!
     * @brief 从参数直接转发构造期待值
     * @tparam Args 构造期待值所用到的参数的类型
     * @param[in] args 构造期待值所用到的参数
     * @return 返回构造好的期待值的引用
     */
    template <typename... Args>
    T &emplace(Args &&...args) noexcept
    {
        static_assert(std::is_nothrow_constructible<T, Args...>::value, "type T should have nothrow_constructible");
        if (m_has_value)
            destroy_at(std::addressof(m_value));
        else {
            destroy_at(std::addressof(m_error));
            m_has_value = true;
        }
        construct_at(std::addressof(m_value), std::forward<Args>(args)...);
        return m_value;
    }

    /*!
     * @brief 从参数直接转发构造期待值
     * @tparam U 初始化列表的模板参数
     * @tparam Args 构造期待值所用到的参数的类型
     * @param[in] args 构造期待值所用到的参数
     * @param[in] li 构造期待值所用到的参数化列表
     * @return 返回构造好的期待值的引用
     */
    template <typename U, typename... Args>
    T &emplace(std::initializer_list<U> li, Args &&...args) noexcept
    {
        static_assert(std::is_nothrow_constructible<T, std::initializer_list<U> &, Args...>::value,
                      "type T should have a noexcept constructor");
        if (m_has_value)
            destroy_at(std::addressof(m_value));
        else {
            destroy_at(std::addressof(m_error));
        }
        construct_at(std::addressof(m_value), li, std::forward<Args>(args)...);
        return m_value;
    }

    // TODO:需要swap吗？
    /*!
     * @brief 交换两个Dtk::Core::Dexpected的值
     * @param[in] _x 另一个Dtk::Core::Dexpected对象
     */
    template <typename std::enable_if<std::is_move_constructible<T>::value and std::is_move_constructible<E>::value and
                                          (std::is_nothrow_move_constructible<T>::value or
                                           std::is_nothrow_move_constructible<E>::value),
                                      bool>::type = true>
    void
    swap(Dexpected &_x) noexcept(std::is_nothrow_move_constructible<T>::value and std::is_nothrow_move_constructible<E>::value)
    {
        if (m_has_value) {
            if (_x.m_has_value) {
                using std::swap;
                swap(m_value, _x.m_value);
            } else {
                this->swap_val_err(_x);
            }
        } else {
            if (_x.m_has_value)
                _x.swap_val_err(*this);
            else {
                using std::swap;
                swap(m_error, _x.m_error);
            }
        }
    }

    /*!
     * @brief 重载箭头运算符
     * @return 一个指向期待值的const指针
     */
    const T *operator->() const noexcept
    {
        assert(m_has_value);
        return std::addressof(m_value);
    }

    /*!
     * @brief 重载箭头运算符
     * @return 一个指向期待值的指针
     */
    T *operator->() noexcept
    {
        assert(m_has_value);
        return std::addressof(m_value);
    }

    /*!
     * @brief 重载解引用运算符
     * @return 一个期待值的const左值引用
     */
    const T &operator*() const &noexcept
    {
        assert(m_has_value);
        return m_value;
    }

    /*!
     * @brief 重载解引用运算符
     * @return 一个期待值的左值引用
     */
    T &operator*() &noexcept
    {
        assert(m_has_value);
        return m_value;
    }

    /*!
     * @brief 重载解引用运算符
     * @return 一个期待值的const右值引用
     */
    const T &&operator*() const &&noexcept
    {
        assert(m_has_value);
        return std::move(m_value);
    }

    /*!
     * @brief 重载解引用运算符
     * @return 一个期待值的右值引用
     */
    T &&operator*() &&noexcept
    {
        assert(m_has_value);
        return std::move(m_value);
    }

    /*!
     * @brief bool转换函数
     * @return 表示Dtk::Core::Dexpected是否有值的bool值
     */
    constexpr explicit operator bool() const noexcept { return m_has_value; }

    /*!
     * @brief 判断Dtk::Core::Dexpected是否有值
     * @return 表示是否有值的bool值
     */
    constexpr bool has_value() const noexcept { return m_has_value; }

    /*!
     * @brief 获取Dtk::Core::Dexpected的期待值
     * @return 期待值的const左值引用
     */
    const T &value() const &
    {
        if (likely(m_has_value)) {
            return m_value;
        }
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(m_error));
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的期待值
     * @return 期待值的左值引用
     */
    T &value() &
    {
        if (likely(m_has_value)) {
            return m_value;
        }
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(m_error));
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的期待值
     * @return 期待值的const右值引用
     * @attention 调用后期待值不可用
     */
    const T &&value() const &&
    {
        if (likely(m_has_value)) {
            return m_value;
        }
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(m_error));
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的期待值
     * @return 期待值的右值引用
     * @attention 调用后期待值不可用
     */
    T &&value() &&
    {
        if (likely(m_has_value)) {
            return m_value;
        }
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(m_error));
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的不期待值
     * @return 不期待值的const左值引用
     */
    const E &error() const &noexcept
    {
        assert(!m_has_value);
        return m_error;
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的不期待值
     * @return 不期待值的左值引用
     */
    E &error() &noexcept
    {
        assert(!m_has_value);
        return m_error;
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的不期待值
     * @return 不期待值的const右值引用
     * @attention 调用后不期待值不可用
     */
    const E &&error() const &&noexcept
    {
        assert(!m_has_value);
        return std::move(m_error);
    }

    /*!
     * @brief 获取Dtk::Core::Dexpected的不期待值
     * @return 不期待值的右值引用
     * @attention 调用后不期待值不可用
     */
    E &&error() &&noexcept
    {
        assert(!m_has_value);
        return std::move(m_error);
    }

    // TODO:因为无法确定U转T时是否会抛出异常，所以都按抛出异常来
    /*!
     * @brief 如果有期待值返回期待值，否则返回传入的默认值
     * @tparam U 期待值的类型
     * @param[in] _v 默认的期待值
     * @return 期待值
     */
    template <typename U>
    T value_or(U &&_v) const &
    {
        static_assert(std::is_copy_constructible<T>::value, "type T should have an copy constructor.");
        static_assert(std::is_convertible<U, T>::value, "type U must can be converted to T.");
        if (m_has_value)
            return m_value;
        return static_cast<T>(std::forward<U>(_v));
    }

    /*!
     * @brief 如果有期待值返回期待值，否则返回传入的默认值
     * @tparam U 期待值的类型
     * @param[in] _v 默认的期待值
     * @return 期待值
     * @attention 如果由期待值，调用后原期待值不可用，同时类型U要可以转换成类型T
     */
    template <typename U>
    T value_or(U &&_v) &&
    {
        static_assert(std::is_move_constructible<T>::value, "type T must bu copy_constructible.");
        static_assert(std::is_convertible<U, T>::value, "type U must can be converted to T.");
        if (m_has_value)
            return std::move(m_value);
        return static_cast<T>(std::forward<U>(_v));
    }

    /*!
     *@brief 重载相等运算符
     */
    template <typename U, typename E2, typename std::enable_if<!std::is_void<U>::value, bool>::type = true>
    friend bool
    operator==(const Dexpected &_x,
               const Dexpected<U, E2> &_y) noexcept(noexcept(bool(*_x == *_y)) and noexcept(bool(_x.error() == _y.error())))
    {
        if (_x.has_value())
            return _y.has_value() and bool(*_x == *_y);
        else
            return !_y.has_value() and bool(_x.error() == _x.error());
    }

    /*!
     *@brief 重载相等运算符
     */
    template <typename U>
    friend constexpr bool operator==(const Dexpected &_x, const U &_v) noexcept(noexcept(bool(*_x == _v)))
    {
        return _x.has_value() && bool(*_x == _v);
    }

    /*!
     *@brief 重载相等运算符
     */
    template <typename E2>
    friend constexpr bool operator==(const Dexpected &_x,
                                     const Dunexpected<E2> &_e) noexcept(noexcept(bool(_x.error() == _e.error())))
    {
        return !_x.has_value() && bool(_x.error() == _e.error());
    }

    /*!
     *@brief 交换两个Dtk::Core::Dexpected中的值
     */
    friend void swap(Dexpected &_x, Dexpected &_y) noexcept(noexcept(_x.swap(_y))) { _x.swap(_y); }

private:
    bool m_has_value;
    union
    {
        struct
        {
        } m_invalid;
        T m_value;
        E m_error;
    };
};

/*!
 * @brief 对于Dtk::Core::Dexpected的void偏特化，其他函数参考原模板类
 * @tparam E 不期待值的类型
 */
template <typename E>
class Dexpected<void, E>
{
    static_assert(__dexpected::_can_be_dunexpected<E>(), "type E can't be Dunexpected.");
    static constexpr bool des_value() { return !std::is_trivially_destructible<E>::value; }

    template <typename, typename>
    friend class Dexpected;

    template <typename U, typename G, typename Unex = Dunexpected<E>>
    static constexpr bool __cons_from_Dexpected()
    {
        return std::is_constructible<Unex, Dexpected<U, G> &>::value and std::is_constructible<Unex, Dexpected<U, G>>::value and
               std::is_constructible<Unex, const Dexpected<U, G> &>::value and
               std::is_constructible<Unex, const Dexpected<U, G>>::value;
    }

    template <typename V>
    void assign_err(V &&_v)
    {
        if (m_has_value) {
            construct_at(std::addressof(m_error), std::forward<V>(_v));
            m_has_value = false;
        } else {
            m_error = std::forward<V>(_v);
        }
    }

public:
    using value_type = void;
    using error_type = E;
    using dunexpected_type = Dunexpected<E>;
    template <typename U>
    using rebind = Dexpected<U, error_type>;

    constexpr Dexpected() noexcept
        : m_has_value(true)
        , m_void()
    {
    }

    Dexpected(const Dexpected &) = default;

    template <typename std::enable_if<std::is_copy_constructible<E>::value and !std::is_trivially_copy_constructible<E>::value,
                                      bool>::type = true>
    Dexpected(const Dexpected &_x) noexcept(std::is_nothrow_copy_constructible<E>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), _x.m_error);
    }

    Dexpected(Dexpected &&) = default;

    template <typename std::enable_if<std::is_move_constructible<E>::value and !std::is_trivially_move_constructible<E>::value,
                                      bool>::type = true>
    Dexpected(Dexpected &&_x) noexcept(std::is_nothrow_move_constructible<E>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    template <typename U,
              typename G,
              typename std::enable_if<std::is_void<U>::value and std::is_constructible<E, const G &>::value and
                                          !__cons_from_Dexpected<U, G>() and !std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    explicit Dexpected(const Dexpected<U, G> &_x) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), _x.m_error);
    }

    template <typename U,
              typename G,
              typename std::enable_if<std::is_void<U>::value and std::is_constructible<E, const G &>::value and
                                          !__cons_from_Dexpected<U, G>() and std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    Dexpected(const Dexpected<U, G> &_x) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), _x.m_error);
    }

    template <typename U,
              typename G,
              typename std::enable_if<std::is_void<U>::value and std::is_constructible<E, G>::value and
                                          __cons_from_Dexpected<U, G>() and !std::is_convertible<G, E>::value,
                                      bool>::type = true>
    explicit Dexpected(Dexpected<U, G> &&_x) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    template <typename U,
              typename G,
              typename std::enable_if<std::is_void<U>::value and std::is_constructible<E, G>::value and
                                          __cons_from_Dexpected<U, G>() and std::is_convertible<G, E>::value,
                                      bool>::type = true>
    Dexpected(Dexpected<U, G> &&_x) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(_x.m_has_value)
        , m_void()
    {
        if (!m_has_value)
            construct_at(std::addressof(m_error), std::move(_x).m_error);
    }

    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, const G &>::value and !std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    constexpr explicit Dexpected(const Dunexpected<G> &_u) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(false)
        , m_error(_u.error())
    {
    }

    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, const G &>::value and std::is_convertible<const G &, E>::value,
                                      bool>::type = true>
    constexpr Dexpected(const Dunexpected<G> &_u) noexcept(std::is_nothrow_constructible<E, const G &>::value)
        : m_has_value(false)
        , m_error(_u.error())
    {
    }

    template <
        typename G = E,
        typename std::enable_if<std::is_constructible<E, G>::value and !std::is_convertible<G, E>::value, bool>::type = true>
    constexpr explicit Dexpected(Dunexpected<G> &&_u) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(false)
        , m_error(std::move(_u).error())
    {
    }

    template <typename G = E,
              typename std::enable_if<std::is_constructible<E, G>::value and std::is_convertible<G, E>::value, bool>::type = true>
    constexpr Dexpected(Dunexpected<G> &&_u) noexcept(std::is_nothrow_constructible<E, G>::value)
        : m_has_value(false)
        , m_error(std::move(_u).error())
    {
    }

    template <typename... Args>
    constexpr explicit Dexpected(emplace_tag) noexcept
        : Dexpected()
    {
    }

    template <typename... Args>
    constexpr explicit Dexpected(dunexpected_tag, Args &&...args) noexcept(std::is_nothrow_constructible<E, Args...>::value)
        : m_has_value(false)
        , m_error(std::forward<Args>(args)...)
    {
        static_assert(std::is_constructible<E, Args...>::value, "type E can't construct from args");
    }

    template <typename U, typename... Args>
    constexpr explicit Dexpected(dunexpected_tag,
                                 std::initializer_list<U> _li,
                                 Args &&...args) noexcept(std::is_nothrow_constructible<E, Args...>::value)
        : m_has_value(false)
        , m_error(_li, std::forward<Args>(args)...)
    {
        static_assert(std::is_constructible<E, std::initializer_list<U> &, Args...>::value, "type E can't construct from args");
    }

    ~Dexpected()
    {
        if (des_value()) {
            destroy_at(std::addressof(m_error));
        }
    }

    Dexpected &operator=(const Dexpected &) = delete;

    template <
        typename std::enable_if<std::is_copy_constructible<E>::value and std::is_copy_assignable<E>::value, bool>::type = true>
    Dexpected &operator=(const Dexpected &_x) noexcept(
        std::is_nothrow_copy_constructible<E>::value and std::is_nothrow_copy_assignable<E>::value)
    {
        if (_x.m_has_value)
            emplace();
        else
            assign_err(_x.m_error);
        return *this;
    }

    template <
        typename std::enable_if<std::is_move_constructible<E>::value and std::is_move_assignable<E>::value, bool>::type = true>
    Dexpected &
    operator=(Dexpected &&_x) noexcept(std::is_nothrow_move_constructible<E>::value and std::is_nothrow_move_assignable<E>::value)
    {
        if (_x.m_has_value)
            emplace();
        else
            assign_err(std::move(_x.m_error));
        return *this;
    }

    template <typename G,
              typename std::enable_if<std::is_constructible<E, const G &>::value and std::is_assignable<E &, const G &>::value,
                                      bool>::type = true>
    Dexpected &operator=(const Dunexpected<G> &_e)
    {
        assign_err(_e.error());
        return *this;
    }

    template <
        typename G,
        typename std::enable_if<std::is_constructible<E, G>::value and std::is_assignable<E &, G>::value, bool>::type = true>
    Dexpected &operator=(Dunexpected<G> &&_e)
    {
        assign_err(std::move(_e.error()));
        return *this;
    }

    void emplace() noexcept
    {
        if (!m_has_value) {
            destroy_at(std::addressof(m_error));
            m_has_value = true;
        }
    }

    template <typename std::enable_if<std::is_move_constructible<E>::value, bool>::type = true>
    void swap(Dexpected &_x) noexcept(std::is_nothrow_move_constructible<E>::value)
    {
        if (m_has_value) {
            if (!_x.m_has_value) {
                construct_at(std::addressof(m_error), std::move(_x.m_error));
                destroy_at(std::addressof(_x.m_error));
                m_has_value = false;
                _x.m_has_value = true;
            }
        } else {
            if (_x.m_has_value) {
                construct_at(std::addressof(_x.m_error), std::move(m_error));
                destroy_at(std::addressof(m_error));
                m_has_value = true;
                _x.m_has_value = false;
            } else {
                using std::swap;
                swap(m_error, _x.m_error);
            }
        }
    }

    constexpr explicit operator bool() const noexcept { return m_has_value; }

    constexpr bool has_value() const noexcept { return m_has_value; }

    void operator*() const noexcept { assert(m_has_value); }

    void value() const &
    {
        if (likely(m_has_value))
            return;
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(m_error));
    }

    void value() &&
    {
        if (likely(m_has_value))
            return;
        _DEXPECTED_THROW_OR_ABORT(bad_result_access<E>(std::move(m_error)));
    }

    const E &error() const &noexcept
    {
        assert(!m_has_value);
        return m_error;
    }

    E &error() &noexcept
    {
        assert(!m_has_value);
        return m_error;
    }

    const E &&error() const &&noexcept
    {
        assert(!m_has_value);
        return std::move(m_error);
    }

    E &&error() &&noexcept
    {
        assert(!m_has_value);
        return std::move(m_error);
    }

    template <typename U, typename E2, typename std::enable_if<std::is_void<U>::value, bool>::type = true>
    friend bool operator==(const Dexpected &_x, const Dexpected<U, E2> &_y) noexcept(noexcept(bool(_x.error() == _y.error())))
    {
        if (_x.has_value())
            return _y.has_value();
        else
            return !_y.has_value() and bool(_x.error() == _y.error());
    }

    template <typename E2>
    friend bool operator==(const Dexpected &_x, const Dunexpected<E2> &_e) noexcept(noexcept(bool(_x.error() == _e.error())))
    {
        return !_x.has_value() && bool(_x.error() == _e.error());
    }

    // TODO:可能没有swap
    friend void swap(Dexpected &_x, Dexpected &_y) noexcept(noexcept(_x.swap(_y))) { _x.swap(_y); }

private:
    bool m_has_value;
    union
    {
        struct
        {
        } m_void;
        E m_error;
    };
};

DCORE_END_NAMESPACE

#endif