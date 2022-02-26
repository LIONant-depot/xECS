namespace xecs::component::type::details
{
    template< typename T_COMPONENT >
    constexpr auto guid_v = []() consteval noexcept -> xecs::component::type::guid
    { return std::is_same_v<xecs::component::entity, T_COMPONENT>
                ? type::guid{ nullptr }
                : T_COMPONENT::typedef_v.m_Guid.m_Value
                ? T_COMPONENT::typedef_v.m_Guid
                : type::guid{ __FUNCSIG__ };
    }();

    //---------------------------------------------------------------------------------
    // Credit: https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/
    consteval
    void UIntToHexString( char* s, const std::uint32_t num, const bool lowerAlpha ) noexcept
    {
        std::uint64_t x = num;

        // use bitwise-ANDs and bit-shifts to isolate
        // each nibble into its own byte
        // also need to position relevant nibble/byte into
        // proper location for little-endian copy
        x = ((x & 0xFFFF) << 32) | ((x & 0xFFFF0000) >> 16);
        x = ((x & 0x0000FF000000FF00) >> 8) | (x & 0x000000FF000000FF) << 16;
        x = ((x & 0x00F000F000F000F0) >> 4) | (x & 0x000F000F000F000F) << 8;

        // Now isolated hex digit in each byte
        // Ex: 0x1234FACE => 0x0E0C0A0F04030201

        // Create bitmask of bytes containing alpha hex digits
        // - add 6 to each digit
        // - if the digit is a high alpha hex digit, then the addition
        //   will overflow to the high nibble of the byte
        // - shift the high nibble down to the low nibble and mask
        //   to create the relevant bitmask
        //
        // Using above example:
        // 0x0E0C0A0F04030201 + 0x0606060606060606 = 0x141210150a090807
        // >> 4 == 0x0141210150a09080 & 0x0101010101010101
        // == 0x0101010100000000
        //
        const std::uint64_t mask = ((x + 0x0606060606060606) >> 4) & 0x0101010101010101;

        // convert to ASCII numeral characters
        x |= 0x3030303030303030;

        // if there are high hexadecimal characters, need to adjust
        // for uppercase alpha hex digits, need to add 0x07
        //   to move 0x3A-0x3F to 0x41-0x46 (A-F)
        // for lowercase alpha hex digits, need to add 0x27
        //   to move 0x3A-0x3F to 0x61-0x66 (a-f)
        // it's actually more expensive to test if mask non-null
        //   and then run the following stmt
        x += ((lowerAlpha) ? 0x27 : 0x07) * mask;

        //copy string to output buffer
        //*(uint64_t*)s = x;
        for (int i = 0; i < 8; ++i) s[i] = static_cast<char>(x >> (i * 8));
    }

    //---------------------------------------------------------------------------------
    struct componen_serialize_name { char s[17]; };
    template< typename T_COMPONENT >
    constexpr static auto serialize_name_v = []() consteval -> componen_serialize_name
    {
        componen_serialize_name Name{};
        uint64_t x = guid_v<T_COMPONENT>.m_Value;
        UIntToHexString( &Name.s[0], static_cast<std::uint32_t>(x>> 0), false );
        UIntToHexString( &Name.s[8], static_cast<std::uint32_t>(x>>32), false );
        return Name;
    }();

    namespace details
    {
        template< typename T_COMPONENT, typename = void >  struct has_full_serialize : std::false_type {};
        template< typename T_COMPONENT >                   struct has_full_serialize< T_COMPONENT, std::void_t<decltype(&T_COMPONENT::FullSerialize)> > : std::true_type
        {
            using fn_traits = xcore::function::traits<decltype(&T_COMPONENT::FullSerialize)>;

            static_assert(std::is_same_v<fn_traits::class_type, void>, "The FullSerialize function should be a STATIC member function");
            static_assert(fn_traits::arg_count_v == 4, "Function must have the same number of arguments, should be 4 --> (xecs::serializer::stream& TextFile, bool isRead, T_COMPONENT_ARRAY*, int& Count ) noexcept");
            static_assert(std::is_same< typename fn_traits::return_type, xcore::err >::value, "The return type should be --> xcore::err ");
            static_assert(xcore::function::details::traits_compare_args<fn_traits, xcore::function::traits<void(*)(xecs::serializer::stream&, bool, T_COMPONENT*, int& Count)>, static_cast<int>(fn_traits::arg_count_v) - 1>::value, "Arguments types don't match they should be (xecs::serializer::stream& TextFile, bool isRead, T_COMPONENT_ARRAY*, int& Count )");

            constexpr static xcore::err FullGenericSerialize(xecs::serializer::stream& TextFile, bool isRead, std::byte* pComponentArray, int& Count) noexcept
            {
                // TODO: Skip records that are not meant to be loaded
                return T_COMPONENT::FullSerialize(TextFile, isRead, reinterpret_cast<T_COMPONENT*>(pComponentArray), Count);
            };

        };

        template< typename T_COMPONENT, typename = void >  struct has_serialize : std::false_type {};
        template< typename T_COMPONENT >                   struct has_serialize< T_COMPONENT, std::void_t<decltype(&T_COMPONENT::Serialize)> > : std::true_type
        {
            using fn_traits = xcore::function::traits<decltype(&T_COMPONENT::Serialize)>;

            static_assert( std::is_same_v<fn_traits::class_type, T_COMPONENT>, "The Serialize function should be a member function" );
            static_assert( fn_traits::arg_count_v == 2, "Function must have the same number of arguments, should be 2 --> (xecs::serializer::stream& TextFile, bool isRead ) noexcept");
            static_assert( std::is_same< typename fn_traits::return_type, xcore::err >::value, "The return type should be --> xcore::err ");
            static_assert( xcore::function::details::traits_compare_args<fn_traits, xcore::function::traits<void(*)(xecs::serializer::stream&, bool)>, static_cast<int>(fn_traits::arg_count_v) - 1>::value, "Arguments types don't match they should be (xecs::serializer::stream& TextFile, bool isRead )");

            constexpr static xcore::err FullGenericSerialize(xecs::serializer::stream& TextFile, bool isRead, std::byte* pComponentArray, int& Count) noexcept
            {
                xcore::err Error;

                // Write a nice comment for the user so he knows what type of component we are writing 
                if constexpr (T_COMPONENT::typedef_v.m_pName) if (isRead == false)
                {
                    Error = TextFile.WriteComment({ T_COMPONENT::typedef_v.m_pName, 1 + std::strlen(T_COMPONENT::typedef_v.m_pName) });
                    if (Error) return Error;
                }

                TextFile.Record
                ( Error
                , serialize_name_v<T_COMPONENT>.s
                , [&](std::size_t& C, xcore::err&) noexcept
                {
                    if (isRead) Count = static_cast<int>(C);
                    else        C = Count;
                }
                , [&](std::size_t c, xcore::err& Error) noexcept
                {
                    Error = std::invoke(&T_COMPONENT::Serialize, reinterpret_cast<T_COMPONENT*>(pComponentArray) + c, TextFile, isRead);
                });

                return Error;
            };
        };
    }

    //---------------------------------------------------------------------------------
    template< typename T_COMPONENT >
    constexpr auto serialize_function_v = []() consteval noexcept -> type::full_serialize_fn*
    {
        if constexpr (T_COMPONENT::typedef_v.id_v == type::id::TAG) return nullptr;
        else
        {
                 if constexpr ( details::has_serialize<T_COMPONENT>::value      ) return details::has_serialize<T_COMPONENT>::FullGenericSerialize;
            else if constexpr ( details::has_full_serialize<T_COMPONENT>::value ) return details::has_full_serialize<T_COMPONENT>::FullGenericSerialize;
            else return nullptr;
        }
    }();

    //---------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    consteval const xcore::property::table* getPropertyTable(void) noexcept
    {
        if constexpr (property::isValidTable<T_COMPONENT>())
        {
            return &xcore::property::getTableByType<T_COMPONENT>();
        }
        else
        {
            return nullptr;
        }
    }

    //---------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    constexpr auto serialize_mode_v = []() constexpr noexcept ->serialize_mode
    {
        if constexpr (T_COMPONENT::typedef_v.id_v == type::id::TAG)
        {
            return serialize_mode::DONT_SERIALIZE;
        }
        else
        {
            if constexpr (T_COMPONENT::typedef_v.m_SerializeMode == serialize_mode::AUTO)
            {
                if constexpr ( serialize_function_v<T_COMPONENT> != nullptr ) return serialize_mode::BY_SERIALIZER;
                else if constexpr (getPropertyTable<T_COMPONENT>()) return serialize_mode::BY_PROPERTIES;
                else return serialize_mode::DONT_SERIALIZE;
            }
            else
            {
                if constexpr (T_COMPONENT::typedef_v.m_SerializeMode == serialize_mode::BY_PROPERTIES)
                {
                    static_assert( serialize_function_v<T_COMPONENT> == nullptr );
                }
                else if constexpr (T_COMPONENT::typedef_v.m_SerializeMode == serialize_mode::BY_PROPERTIES)
                {
                    static_assert(getPropertyTable<T_COMPONENT>());
                }

                return T_COMPONENT::typedef_v.m_SerializeMode;
            }
        }
    }();
    
    namespace details
    {
        template< typename T_COMPONENT, typename = void >  struct has_report_references : std::false_type {};
        template< typename T_COMPONENT >                   struct has_report_references< T_COMPONENT, std::void_t<decltype(&T_COMPONENT::ReportReferences)> > : std::true_type
        {
            using fn_traits = xcore::function::traits<decltype(&T_COMPONENT::ReportReferences)>;

            static_assert(std::is_same_v<fn_traits::class_type, T_COMPONENT>, "The ReportReferences should be a member function and NOT a STATIC member function");
            static_assert(fn_traits::arg_count_v == 1, "Function must have the same number of arguments, should be 1 --> ( std::vector<xecs::component::entity*>& ) noexcept");
            static_assert(std::is_same< typename fn_traits::return_type, void >::value, "The return type should be --> void");
            static_assert(xcore::function::details::traits_compare_args<fn_traits, xcore::function::traits<void(*)(std::vector<xecs::component::entity*>&)>, static_cast<int>(fn_traits::arg_count_v) - 1>::value, "Arguments types don't match they should be (std::vector<xecs::component::entity*>& )");

            constexpr static void FullGenericReportReferences( std::vector<xecs::component::entity*>& List, std::byte* pComponent ) noexcept
            {
                std::invoke(&T_COMPONENT::ReportReferences, reinterpret_cast<T_COMPONENT*>(pComponent), List);
            };
        };
    }

    //---------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    constexpr auto references_mode_v = []() consteval noexcept -> type::reference_mode
    {
        if constexpr (T_COMPONENT::typedef_v.id_v == type::id::TAG) return type::reference_mode::NO_REFERENCES;
        else if constexpr (T_COMPONENT::typedef_v.m_ReferenceMode == type::reference_mode::NO_REFERENCES) return type::reference_mode::NO_REFERENCES;
        else
        {
            if constexpr (T_COMPONENT::typedef_v.m_ReferenceMode == type::reference_mode::BY_FUNCTION)
            {
                static_assert(details::has_report_references<T_COMPONENT>::value, "You forgot to add a ReportReference Member Function");
                return type::reference_mode::BY_FUNCTION;
            }
            else if constexpr (T_COMPONENT::typedef_v.m_ReferenceMode == type::reference_mode::BY_PROPERTIES)
            {
                static_assert(getPropertyTable<T_COMPONENT>(), "You must have properties");
                return type::reference_mode::BY_PROPERTIES;
            }
            else
            {
                static_assert(T_COMPONENT::typedef_v.m_ReferenceMode == type::reference_mode::AUTO);

                if constexpr (details::has_report_references<T_COMPONENT>::value) return type::reference_mode::BY_FUNCTION;
                else if constexpr (property::isValidTable<T_COMPONENT>() == false) return type::reference_mode::NO_REFERENCES;
                else
                {
                    auto& Table = xcore::property::getTableByType<T_COMPONENT>();

                    for (int i = 0; i < Table.m_Count; ++i)
                    {
                        // is in one of the core properties?
                        if (xcore::types::variant_t2i_v< xecs::component::entity, xcore::property::settings::data_variant> == Table.m_pActionEntries[i].m_FunctionTypeGetSet.index())
                            return type::reference_mode::BY_PROPERTIES;

                        if (Table.m_pActionEntries[i].m_FunctionLists || Table.m_pActionEntries[i].m_FunctionTypeGetSet.index() >= std::variant_size_v<xcore::property::settings::data_variant>)
                        {
                            // IF IT DID NOT COMPILED IS BECAUSE THIS ASSERT TRIGGER, PLEASE READ IT...
                            assert(xcore::types::always_false_v<T_COMPONENT> && "Component must specify the typedef_v.m_ReferenceMode and can not be AUTO, since we can not determine if it has references or not");
                            return type::reference_mode(0xff);
                        }
                    }

                    return type::reference_mode::NO_REFERENCES;
                }
            }
        }
    }();

    //---------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    constexpr auto references_v = []() consteval noexcept -> report_references_fn*
    {
             if constexpr (T_COMPONENT::typedef_v.id_v == type::id::TAG) return nullptr;
        else if constexpr (references_mode_v<T_COMPONENT> == reference_mode::NO_REFERENCES) return nullptr;
        else if constexpr (references_mode_v<T_COMPONENT> == reference_mode::BY_PROPERTIES) return nullptr;
        else
        {
            static_assert(details::has_report_references<T_COMPONENT>::value, "You must provide a m_pReportReferenceFn");
            return &details::has_report_references<T_COMPONENT>::FullGenericReportReferences;
        }
    }();
    
    //---------------------------------------------------------------------------------

    template< typename T_COMPONENT >
    consteval
    type::info CreateInfo( void ) noexcept
    {
        static_assert( xecs::component::type::is_valid_v<T_COMPONENT> );
        return type::info
        {
            .m_Guid             = guid_v<T_COMPONENT>
        ,   .m_BitID            = info::invalid_bit_id_v
        ,   .m_Size             = xcore::types::static_cast_safe<std::uint16_t>(sizeof(T_COMPONENT))
        ,   .m_TypeID           = T_COMPONENT::typedef_v.id_v
        ,   .m_bGlobalScoped    = []{ if constexpr (T_COMPONENT::typedef_v.id_v == type::id::SHARE) return T_COMPONENT::typedef_v.m_bGlobalScoped; else return false; }()
        ,   .m_bBuildShareFilter= []{ if constexpr (T_COMPONENT::typedef_v.id_v == type::id::SHARE) return T_COMPONENT::typedef_v.m_bBuildFilter; else return false; }()
        ,   .m_bExclusiveTag    = []{ if constexpr (T_COMPONENT::typedef_v.id_v == type::id::TAG)   return T_COMPONENT::typedef_v.exclusive_v;     else return false; }()
        ,   .m_pConstructFn     = std::is_trivially_constructible_v<T_COMPONENT>
                                    ? nullptr
                                    : []( std::byte* p ) noexcept
                                    {
                                        new(p) T_COMPONENT;
                                    }
        ,   .m_pDestructFn      = std::is_trivially_destructible_v<T_COMPONENT>
                                    ? nullptr
                                    : []( std::byte* p ) noexcept
                                    {
                                        std::destroy_at(reinterpret_cast<T_COMPONENT*>(p));
                                    }
        ,   .m_pMoveFn          = std::is_trivially_move_assignable_v<T_COMPONENT>
                                    ? nullptr
                                    : []( std::byte* p1, std::byte* p2 ) noexcept
                                    {
                                        *reinterpret_cast<T_COMPONENT*>(p1) = std::move(*reinterpret_cast<T_COMPONENT*>(p2));
                                    }
        ,   .m_pCopyFn          = std::is_trivially_copy_assignable_v<T_COMPONENT>
                                    ? nullptr
                                    : []( std::byte* p1, const std::byte* p2 ) noexcept
                                    {
                                        *reinterpret_cast<T_COMPONENT*>(p1) = *reinterpret_cast<const T_COMPONENT*>(p2);
                                    }
        ,   .m_pComputeKeyFn    = []() consteval noexcept ->type::share::compute_key_fn*
                                  {
                                        if constexpr (T_COMPONENT::typedef_v.id_v != type::id::SHARE) return nullptr;
                                        else if constexpr(T_COMPONENT::typedef_v.m_ComputeKeyFn)
                                        {
                                            return [](const std::byte* p) constexpr noexcept -> type::share::key
                                            {
                                                constexpr auto Guid = guid_v<T_COMPONENT>;
                                                return { T_COMPONENT::typedef_v.m_ComputeKeyFn(p).m_Value
                                                            + Guid.m_Value };
                                            };
                                        }
                                        else return [](const std::byte* p) constexpr noexcept -> type::share::key
                                        {
                                            constexpr auto Guid = guid_v<T_COMPONENT>;
                                            return { xcore::crc<64>{}.FromBytes( {p,sizeof(T_COMPONENT)}, Guid.m_Value ).m_Value };
                                        };
                                  }()
        ,   .m_pSerilizeFn          = serialize_function_v<T_COMPONENT>
        ,   .m_pReportReferencesFn  = references_v<T_COMPONENT>
        ,   .m_pPropertyTable       = getPropertyTable<T_COMPONENT>()
        ,   .m_SerializeMode        = serialize_mode_v<T_COMPONENT> 
        ,   .m_ReferenceMode        = references_mode_v<T_COMPONENT>
        ,   .m_pName                = T_COMPONENT::typedef_v.m_pName
                                    ? T_COMPONENT::typedef_v.m_pName
                                    : __FUNCSIG__
        };
    }

    //-------------------------------------------------------------------------------------

    constexpr
    type::share::key ComputeShareKey( xecs::archetype::guid Guid, const type::info& TypeInfo, const std::byte* pData = nullptr ) noexcept
    {
        if(pData)
        {
            auto Key = TypeInfo.m_pComputeKeyFn(pData);
            if (TypeInfo.m_bGlobalScoped) return Key;
            return { Guid.m_Value + Key.m_Value };

        }
        else
        {
            if (TypeInfo.m_bGlobalScoped) return TypeInfo.m_DefaultShareKey;
            return { Guid.m_Value + TypeInfo.m_DefaultShareKey.m_Value };
        }
    }

    //
    // Sort component types
    //

    //------------------------------------------------------------------------------------------------------
    constexpr __inline
    bool CompareTypeInfos( const xecs::component::type::info* pA, const xecs::component::type::info* pB ) noexcept
    {
        if ((int)pA->m_TypeID == (int)pB->m_TypeID)
        {
            return pA->m_Guid < pB->m_Guid;
        }
        return (int)pA->m_TypeID < (int)pB->m_TypeID;
    }

    //------------------------------------------------------------------------------------------------------
    template< typename T_A, typename T_B >
    struct smaller_component
    {
        constexpr static bool value = CompareTypeInfos( &xecs::component::type::info_v<T_A>, &xecs::component::type::info_v<T_B> );
    };

    template< typename T_TUPLE >
    requires(xcore::types::is_specialized_v<std::tuple, T_TUPLE>)
    using sort_tuple_t = xcore::types::tuple_sort_t<smaller_component, T_TUPLE>;

    template< typename T_TUPLE >
    static constexpr auto sorted_info_array_v = []<typename...T>(std::tuple<T...>*) constexpr
    {
        if constexpr (sizeof...(T) == 0 )   return std::span<const xecs::component::type::info*>{};
        else                                return std::array{ &component::type::info_v<T>... };
    }(xcore::types::null_tuple_v< sort_tuple_t<T_TUPLE> >);

    template< typename...T_TUPLES_OR_COMPONENTS >
    using combined_t = xcore::types::tuple_cat_t
    <   std::conditional_t
        <
            xcore::types::is_specialized_v<std::tuple, T_TUPLES_OR_COMPONENTS>
        ,   T_TUPLES_OR_COMPONENTS
        ,   std::tuple<T_TUPLES_OR_COMPONENTS>
        > ...
    >;

    template< typename T_TUPLE >
    using share_only_tuple_t = std::invoke_result_t
    <
        decltype
        (   []<typename...T>(std::tuple<T...>*) ->
            xcore::types::tuple_cat_t
            < std::conditional_t
                < xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::SHARE
                , std::tuple<T>
                , std::tuple<>
                >
            ...
            > {}
        )
    , T_TUPLE*
    >;

    template< typename T_TUPLE >
    using data_only_tuple_t = std::invoke_result_t
    <   decltype
        (   []<typename...T>(std::tuple<T...>*) ->
            xcore::types::tuple_cat_t
            < std::conditional_t
                < xecs::component::type::info_v<T>.m_TypeID == xecs::component::type::id::DATA
                , std::tuple<T>
                , std::tuple<>
                >
            ...
            > {}
        )
    ,   T_TUPLE*
    >;
}
