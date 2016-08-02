//=============================================================================
//       _   _   _   _____
//      | | | \ | | |_   _|    _     _
//      | | |  \| |   | |    _| |_ _| |_
//  _   | | | . ` |   | |   |_   _|_   _|
// | |__| | | |\  |  _| |_    |_|   |_|
//  \____/  |_| \_| |_____|
//!
//! \file    jnipp/jnipp.hpp
//! \author  Arata Furukawa
//!          GitHub : https://github.com/ornew
//!          Email  : info@ornew.net
//! \brief   JNI++
//! \version v0.0.1
//! \date    2016-
//=============================================================================
#ifndef JNIPP_JNIPP_HPP
#define JNIPP_JNIPP_HPP

#include <cstdint>
#include <string>
#include <type_traits>
#include <memory>

#include <jni.h>

namespace ornew {
    struct constructor_tag {
    };
    static constexpr constructor_tag constructor = {};
    template<typename Type>
    class storage {
    public:
        using type = Type;
        using self = storage<type>;
        using buffer = typename std::aligned_storage<sizeof(type), alignof(type)>::type;
    private:
        buffer buf;
        bool flag;

    public:
        storage(std::nullptr_t)
            : flag{false} {
        }
        storage(self const& a){
            assign(*a.raw());
        }
        storage(type const& a){
            assign(a);
        }
        template<typename... Args>
        storage(constructor_tag, Args&&... a){
            construct(std::forward<Args>(a)...);
        }
        ~storage(){
            destruct();
        }
        type const* raw() const
        {
            // type erasure and force cast
            return static_cast<type const*>(static_cast<void const*>(&buf));
        }
        type* raw()
        {
            // type erasure and force cast
            return static_cast<type*>(static_cast<void*>(&buf));
        }
        template<typename... Args>
        self const& construct(Args&&... a){
            destruct();
            new(raw()) type(std::forward<Args>(a)...);
            flag = true;
            return *this;
        }
        void destruct(){
            if(flag){
                raw()->~type();
                flag = false;
            }
        }
        void assign(type const& a){
            destruct();
            (*raw()) = a;
        }
        void operator=(type const& a){
            assign(a);
        }
        type* operator ->(){
            return raw();
        }
    };
    namespace error {
        struct basic_error {
        private:
            std::string _message;
        public:
            basic_error(){}
            template<typename String>
            basic_error(String&& m)
                :_message{ std::forward<String>(m) }{
            }

            std::string const& get_message() const {
                return _message;
            }
        };
        struct runtime_error
            :public basic_error {
        public:
            template<typename... A>
            runtime_error(A... a)
                : basic_error{ std::forward<A>(a)... } {
            }
        };
    }
    template<typename Result, typename Error = error::basic_error>
    class expected;
    template<typename Error>
    class unexpected {
    public:
        using error = Error;
    private:
        std::unique_ptr<error> e;

    public:
        template<typename... E>
        unexpected(E&&... e)
            : e{ new Error{ std::forward<E>(e)... } }
        {}
        std::unique_ptr<error> move_error(){
            return std::move(e);
        }
    };
    template<typename Result, typename Error>
    class expected {
    public:
        using error = Error;
        using result = Result;

    private:
        std::unique_ptr<error> e;
        storage<result> r;

    public:
        expected()
            : r{ nullptr }{
        }
        expected(storage<result> a)
            : r{ a }{
        }
        expected(result a)
            : r{ a }{
        }
        expected(unexpected<error>&& e)
            : r{ nullptr }{
            this->e = std::move(e.move_error());
        }
    };
    template<typename Error, typename... Args>
    static auto raise(Args&&... a){
        return unexpected<Error>{ std::forward<Args>(a)... };
    }
}

namespace jnipp {
    struct jni_error
        : public ornew::error::runtime_error {
    private:
        JNIEnv* env;

    public:
        template<typename... Args>
        jni_error(JNIEnv* env, Args&&... a): env{ env }, ornew::error::runtime_error{ std::forward<Args>(a)... }{}
        void fatal(){
            env->FatalError(get_message().c_str());
        }
    };

    template<typename T>
    using jni_expected = ornew::expected<T,jni_error>;

    template<typename... Args>
    auto jni_raise(JNIEnv* env, Args&&... a){
        return ornew::raise<jni_error>(env, std::forward<Args>(a)...);
    }


    template <typename Type> struct resolver {};

    template <> struct resolver<void> { using type = void; };
    template <> struct resolver<bool> { using type = jboolean; };
    template <> struct resolver<char> { using type = jbyte; };
    template <> struct resolver<unsigned char> { using type = jchar; };
    template <> struct resolver<std::int16_t> { using type = jshort; };
    template <> struct resolver<std::int32_t> { using type = jint; };
    template <> struct resolver<std::int64_t> { using type = jlong; };
    template <typename Return, typename... Args> struct resolver<Return(Args...)> {
        using return_type = Return;
        using type = typename resolver<Return>::type(typename resolver<Args>::type...);
    };

    template <typename Type> using type = typename resolver<Type>::type;


    namespace detail{
        inline void pack_to_string(std::string&)
        {
        }
        template<typename First, typename... Rest>
        void pack_to_string(std::string& s, const First& c, const Rest&... rest)
        {
            s += c;
            pack_to_string(s, rest...);
        }
    }
    template<char... values>
    struct pack{
        static const char str[];
    };
    template<char... values>
    const char pack<values...>::str[] = { values..., '\0' };

    namespace detail{
        template<typename,typename>
        struct _pack_join{};
        template<char... left, char... right>
        struct _pack_join<pack<left...>,pack<right...>>{
            using type = pack<left..., right...>;
        };
    }
    template<typename L, typename R>
    using pack_join = typename detail::_pack_join<L,R>::type;

    namespace detail{
        template<typename,typename,typename...>
        struct _pack_join_all{};
        template<typename Left, typename Right>
        struct _pack_join_all<Left, Right>{
            using type = pack_join<Left, Right>;
        };
        template<typename Left, typename Right, typename Next, typename... Rest>
        struct _pack_join_all<Left, Right, Next, Rest...>{
            using type = typename _pack_join_all<pack_join<Left, Right>, Next, Rest...>::type;
        };
    }
    template<typename... T>
    using pack_join_all = typename detail::_pack_join_all<T...>::type;

    template<typename>
    struct defined {};
    template<typename Type, typename = void>
    struct mangler{
    };
    template<> struct mangler<jboolean>{ using name = pack<'Z'>; };
    template<> struct mangler<jbyte>{ using name = pack<'B'>; };
    template<> struct mangler<jchar>{ using name = pack<'C'>; };
    template<> struct mangler<jshort>{ using name = pack<'S'>; };
    template<> struct mangler<jint>{ using name = pack<'I'>; };
    template<> struct mangler<jlong>{ using name = pack<'L'>; };
    template<> struct mangler<jfloat>{ using name = pack<'F'>; };
    template<> struct mangler<jdouble>{ using name = pack<'D'>; };

    template<typename L> struct mangler<defined<L>>{
        using name = pack_join_all<pack<'L'>, typename L::name, pack<';'>>;
    };

    template<typename Type>
    struct mangler<Type, std::enable_if_t<std::is_pointer<Type>::value>>{
        using name = pack_join<pack<'['>, typename mangler<std::remove_pointer_t<Type>>::name>;
    };

    template<typename Return, typename... Args>
    struct mangler<Return(Args...)> {
        using name =
            pack_join_all<
                pack<'('>,
                typename mangler<Args>::name...,
                pack<')'>,
                typename mangler<Return>::name >;
    };

    template<typename Type>
    using mangle = typename mangler<Type>::name;

    struct jstring_define {
        using name = pack<'j','a','v','a','/','l','a','n','g','/','S','t','r','i','n','g'>;
    };
    using jstring = defined<jstring_define>;

    class clas;

    class virtual_machine {
    };
    using vm = virtual_machine;

    class environment {
    private:
        JNIEnv* env;
    public:
        jni_expected<clas> find_class(std::string name);
        // no const
        JNIEnv* attach() {
            return env;
        }
    };
    using env = environment;

    class method_id {
    protected:
        environment* env;
        jclass cls;
        jmethodID id;
    public:
        method_id(environment* env, jclass c, jmethodID id)
            : env{env}, cls{c}, id{id} {}
    };
    template<typename>
    class method;
#define JNIPP_METHOD_MAP(type, name) \
    template<> class method <type> : public method_id { \
        public: template<typename... Args> type operator()(Args&&... a){ \
            return env->attach()->Call##name##Method(cls, id, std::forward<Args>(a)...); } };
    JNIPP_METHOD_MAP(void, Void)
    JNIPP_METHOD_MAP(jboolean, Boolean)
    JNIPP_METHOD_MAP(jbyte, Byte)
    JNIPP_METHOD_MAP(jchar, Char)
    JNIPP_METHOD_MAP(jshort, Short)
    JNIPP_METHOD_MAP(jint, Int)
    JNIPP_METHOD_MAP(jlong, Long)
    JNIPP_METHOD_MAP(jfloat, Float)
    JNIPP_METHOD_MAP(jdouble, Double)
#undef JNIPP_METHOD_MAP

    class clas {
    private:
        environment* env;
        jclass c;
    public:
        clas(environment* env, jclass c): env{env}, c{c} {}
        template<typename Signature, typename type = jnipp::type<Signature>>
        auto get_method(std::string name) -> jni_expected<method<typename type::return_type>> {
            auto id = env->attach()->GetMethodID(c, name.c_str(), mangle<type>::str);
            if(id == NULL){
                return jni_raise(env->attach(), "Not found: " + name + " in clas::get_method function.");
            }
            return method<typename type::return_type>{ env, c, id };
        }
    };

    jni_expected<clas> environment::find_class(std::string name){
        jclass c = env->FindClass(name.c_str());
        if(c == NULL){
            return jni_raise(env, "Not found: " + name + " in env::find_class function.");
        }
        return clas{ this, c };
    }
}
#endif // JNIPP_JNIPP_HPP
