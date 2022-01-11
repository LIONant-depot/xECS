namespace xecs::game_mgr
{
    //---------------------------------------------------------------------------

    void instance::Run(void) noexcept
    {
        if (m_isRunning == false)
        {
            m_isRunning = true;
            m_ArchetypeMgr.UpdateStructuralChanges();
            m_SystemMgr.m_Events.m_OnGameStart.NotifyAll();
        }

        XCORE_PERF_FRAME_MARK()
        XCORE_PERF_FRAME_MARK_START("ecs::Frame")

        //
        // Run systems
        //
        m_SystemMgr.Run();

        XCORE_PERF_FRAME_MARK_END("ecs::Frame")
    }

    //---------------------------------------------------------------------------

    void instance::Stop(void) noexcept
    {
        if (m_isRunning)
        {
            m_isRunning = false;
            m_SystemMgr.m_Events.m_OnGameEnd.NotifyAll();
        }
    }

    //---------------------------------------------------------------------------

    constexpr static auto textfile_property_types_v = []() constexpr
    {
        std::array<xcore::textfile::user_defined_types, std::variant_size_v<property::settings::data_variant> > List{};

                                                                                                       // prop_types   textfile types
                                                                                                       // ------------ -------------
        List[xcore::types::variant_t2i_v<int,                       property::settings::data_variant>] = { "d",         "d"  };
        List[xcore::types::variant_t2i_v<bool,                      property::settings::data_variant>] = { "b",         "c"  };
        List[xcore::types::variant_t2i_v<float,                     property::settings::data_variant>] = { "f",         "f"  };
        List[xcore::types::variant_t2i_v<string_t,                  property::settings::data_variant>] = { "s",         "s"  };
        List[xcore::types::variant_t2i_v<xcore::math::vector2,      property::settings::data_variant>] = { "v2",        "ff" };
        List[xcore::types::variant_t2i_v<xecs::component::entity,   property::settings::data_variant>] = { "entity",    "G"  };
        List[xcore::types::variant_t2i_v<std::int16_t,              property::settings::data_variant>] = { "C",         "C"  };

        static_assert( std::variant_size_v<property::settings::data_variant> == 7, "You need to change code inside this function ");
        return List;
    }();

    //---------------------------------------------------------------------------

    constexpr static auto textfile_porperty_data_to_index( const xcore::property::settings::data_variant& PropData ) noexcept
    {
        return std::visit( [&]( auto&& Value ) constexpr noexcept
        {
            using   T = std::decay_t<decltype(Value)>;
            return xcore::types::variant_t2i_v<T, xcore::property::settings::data_variant>;
        }
        , PropData
        );
    }

    //---------------------------------------------------------------------------

    xcore::err instance::SerializeComponentWithProperties
    ( bool                                  isRead
    , xcore::textfile::stream&              TextFile
    , std::string&                          PropName
    , xcore::property::data&                PropData
    ) noexcept
    {
        xcore::err Error;

        if(isRead)
        {

        }
        else
        {
            if(Error = TextFile.Field( "Name", PropName ); Error) return Error;

            std::visit( [&]( auto&& Value ) //constexpr noexcept
            {
                using           T                   = std::decay_t<decltype( Value )>;
                constexpr auto  prop_type_index_v   = xcore::types::variant_t2i_v<T, xcore::property::settings::data_variant>;
                constexpr auto  CRC                 = textfile_property_types_v[prop_type_index_v].m_CRC;

                if constexpr ( std::is_same_v<T, xcore::vector2> )
                {
                    Error = TextFile.Field(CRC, "Data:?", Value.m_X, Value.m_Y );
                }
                else if constexpr (std::is_same_v<T, xecs::component::entity>)
                {
                    Error = TextFile.Field(CRC, "Data:?", Value.m_Value);
                }
                else
                {
                    Error = TextFile.Field(CRC, "Data:?", Value );
                }
            }
            , PropData
            );
        }

        return Error;
    }

    //---------------------------------------------------------------------------

    xcore::err instance::SerializeComponentWithProperties
    ( bool                                          isRead
    , xcore::textfile::stream&                      TextFile
    , const xecs::component::entity::global_info&   EntityInfo
    , const xecs::component::type::info&            Info
    , std::byte*                                    pDataBase
    ) noexcept
    {
        xcore::err Error;
        if( isRead )
        {

        }
        else
        {
            std::vector<property::entry>    PropertyList;

            // Collect all the properties for a single component
            PropertyList.clear();
            property::SerializeEnum
            ( *Info.m_pPropertyTable
            , pDataBase + Info.m_Size * EntityInfo.m_PoolIndex.m_Value
            , [&](std::string_view PropertyName, property::data&& Data, const property::table&, std::size_t, property::flags::type Flags) constexpr noexcept
            {
                // If we are dealing with a scope that is not an array someone may have change the SerializeEnum to a DisplayEnum they only show up there.
                assert(Flags.m_isScope == false || PropertyName.back() == ']');
                if (Flags.m_isDontSave || Flags.m_isScope) return;
                PropertyList.push_back(property::entry { PropertyName, Data });
            });

            // Serialize all the properties
            TextFile.Record
            ( Error
            , "Props"
            , [&](std::size_t& C, xcore::err&) noexcept
            {
                assert(isRead == false);
                C = PropertyList.size();
            }
            , [&]( std::size_t c, xcore::err& Err ) noexcept
            {
                auto& PropEntry = PropertyList[c];
                Err = SerializeComponentWithProperties( isRead, TextFile, PropEntry.first, PropEntry.second );
            });
        }
        
        return Error;
    }

    //---------------------------------------------------------------------------
    namespace details
    {
        enum serialized_mode : std::int8_t
        { MODE_NONE
        , MODE_SERIALIZER
        , MODE_PROPERTY            
        };

        struct component_serialized_mode
        {
            xecs::component::type::guid         m_Guid;
            int                                 m_Mode;
            const xecs::component::type::info*  m_pInfo;
        };
    }

    xcore::err instance::SerializeGameState
    ( const char* pFileName
    , bool        isRead
    , bool        isBinary
    ) noexcept
    {
        std::array<xecs::component::entity,             xecs::settings::max_share_components_per_entity_v> ShareEntities{};
        std::array<xecs::component::type::share::key,   xecs::settings::max_share_components_per_entity_v> ShareKeys    {};

        xcore::textfile::stream     TextFile;
        xcore::err                  Error;

        //
        // Have the file system know the mapping of the property types
        //
        TextFile.AddUserTypes( textfile_property_types_v );
         
        //
        // Make sure that we are all up to date
        //
        if(isRead == false) m_ArchetypeMgr.UpdateStructuralChanges();

        //
        // Open file for writing
        //
        if(Error = TextFile.Open
        ( isRead
        , std::string_view{ pFileName }
        , isBinary ? xcore::textfile::file_type::BINARY : xcore::textfile::file_type::TEXT
        , {xcore::textfile::flags::WRITE_FLOATS}
        ); Error ) return Error;

        //
        // Serialize some basic info
        //
        int ArchetypeCount = isRead 
        ? 0 
        : [&]
        {
            // We only serialize archetypes that are not prefabs...
            // Note that prefabs-variants are also prefabs but with the extra xecs::prefab::instance component
            int Count = 0;
            for( const auto& E : m_ArchetypeMgr.m_lArchetype )
            {
                if( false == E->getComponentBits().getBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID) )
                {
                    Count++;
                }
            }
            return Count;
        }();

        if (TextFile.Record(Error, "GameMgr"
            , [&](std::size_t i, xcore::err& Error) noexcept
            {
                Error = TextFile.Field("nArchetypes", ArchetypeCount);
            }
        )) return Error;

        // If we are writing set the count back to the true count
        // this would allow us to filter later
        if( isRead == false ) ArchetypeCount = static_cast<int>(m_ArchetypeMgr.m_lArchetype.size());

        //
        // Write Global Guids
        //
        if( TextFile.Record( Error, "GlobalEntities"
        ,   [&]( std::size_t& C, xcore::err& ) noexcept
            {
                if( false == isRead )
                {
                    // We will save at least one entry even if the validation is zero
                    // because we don't want to deal with the case where there is block does not save
                    C = 1;

                    //
                    // Determine the max index we want to write
                    //
                    auto Span = std::span{ m_ComponentMgr.m_lEntities.get(), xecs::settings::max_entities_v };
                    for (auto It = Span.rbegin(); It != Span.rend(); ++It)
                    {
                        auto& E = *It;
                        if (E.m_Validation.m_Value)
                        {
                            C = 1 + static_cast<int>(static_cast<std::size_t>(&E - m_ComponentMgr.m_lEntities.get()));
                            break;
                        }
                    }
                }
            }
        ,   [&]( std::size_t i, xcore::err& Error )
            {
                Error = TextFile.Field("Validation", m_ComponentMgr.m_lEntities[i].m_Validation.m_Value );
            }
        )) return Error;

        //
        // Serialize all the archetypes
        //
        for( int iArchetype=0; iArchetype < ArchetypeCount; ++iArchetype)
        {
            // When writing we want to filter out any prefab archetype
            if( false == isRead && m_ArchetypeMgr.m_lArchetype[iArchetype]->getComponentBits().getBit(xecs::component::type::info_v<xecs::prefab::tag>.m_BitID))
                continue;

            //
            // Archetype main header
            //
            xecs::component::entity::info_array Infos           {};
            int                                 InfoCount       {};
            int                                 nDataTypes      {};
            int                                 nShareTypes     {};
            int                                 nTagTypes       {};
            int                                 nFamilies       {};
            xecs::archetype::guid               ArchetypeGuid   {};
            std::array<std::int8_t, xecs::settings::max_components_per_entity_v> SerializedModes {};

            if( false == isRead )
            {
                auto&               Archetype   = m_ArchetypeMgr.m_lArchetype[iArchetype];
                xecs::tools::bits   TagBits     = xecs::tools::bits{}.setupAnd(Archetype->m_ComponentBits, xecs::component::mgr::s_TagsBits);

                nDataTypes      = Archetype->m_nDataComponents;
                nShareTypes     = Archetype->m_nShareComponents;
                nTagTypes       = TagBits.CountComponents();
                InfoCount       = Archetype->m_ComponentBits.ToInfoArray(Infos);
                nFamilies       = 0;
                ArchetypeGuid   = Archetype->m_Guid;

                // Save only families which have entites
                for( auto pF = Archetype->getFamilyHead(); pF; pF = pF->m_Next.get() )
                {
                    nFamilies++;
                }

                //
                // Write comments to help the user read the file
                //
                if ((Error = TextFile.WriteComment(" TypeInfo Details: "))) return Error;
                for(int i=0; i< InfoCount; i++ )
                {
                    if( (Error = TextFile.WriteComment( xcore::string::Fmt( "   Guid:%.16llX   Type:%s   Name:%s"
                        , Infos[i]->m_Guid.m_Value
                        , Infos[i]->m_TypeID == xecs::component::type::id::DATA 
                            ? "Data"
                            : Infos[i]->m_TypeID == xecs::component::type::id::SHARE
                            ? "Share"
                            : "Tag"
                        , Infos[i]->m_pName ).getView()))) return Error;
                }
            }

            //
            // Save the basic archetype info
            //
            if( TextFile.Record( Error, "Archetype"
                ,   [&]( std::size_t, xcore::err& Error ) noexcept
                    {
                            (Error = TextFile.Field("Guid",         ArchetypeGuid.m_Value))
                        ||  (Error = TextFile.Field("nFamilies",    nFamilies))
                        ||  (Error = TextFile.Field("nDataTypes",   nDataTypes))
                        ||  (Error = TextFile.Field("nShareTypes",  nShareTypes))
                        ||  (Error = TextFile.Field("nTagTypes",    nTagTypes));
                    }
                )) return Error;

            //
            // Read the archetype types
            //
            if( TextFile.Record( Error, "ArchetypeTypes"
                ,   [&]( std::size_t& C, xcore::err& ) noexcept
                    {
                        if( isRead ) InfoCount  = static_cast<int>(C); 
                        else         C          = InfoCount;
                    }
                ,   [&]( std::size_t i, xcore::err& Error ) noexcept
                    {
                        xecs::component::type::guid Guid
                            = isRead
                            ? xecs::component::type::guid{}
                            : Infos[i]->m_Guid;
                        
                        if( (Error = TextFile.Field( "TypeGuid", Guid.m_Value )) ) return;

                        if( isRead )
                        {
                            auto pInfo = m_ComponentMgr.findComponentTypeInfo(Guid);
                            if( pInfo == nullptr )
                            {
                                // TODO: Change this to a warning?
                                Error = xerr_failure_s("Serialization Error, Fail to find one of the components types");
                                return;
                            }

                            // Set the info into the structure this includes tags
                            Infos[i] = pInfo;
                        }

                        // We need to know how this type was serialized when loading. This is only relevant when
                        // we saved with properties and then we add the serializing function, for the loader to do the
                        // right thing it should used the properties loader since this is how it was saved with.
                        if( isRead == false )
                        {
                            if( Infos[i]->m_pSerilizeFn         ) SerializedModes[i] = details::serialized_mode::MODE_SERIALIZER;
                            else if (Infos[i]->m_pPropertyTable ) SerializedModes[i] = details::serialized_mode::MODE_PROPERTY;
                            else                                  SerializedModes[i] = details::serialized_mode::MODE_NONE;
                        }
                        TextFile.Field("SerializationMode", SerializedModes[i]).clear();
                    }
                )) return Error;

            //
            // Get or create the actual archetype
            //
            xecs::archetype::instance* pArchetype
                = isRead
                ? &getOrCreateArchetype({ Infos.data(), static_cast<std::size_t>(InfoCount) })
                : m_ArchetypeMgr.m_lArchetype[iArchetype].get();

            //
            // Serialize all families
            //
            xecs::pool::family* pF = isRead ? nullptr : pArchetype->getFamilyHead();
            for( int iFamily = 0; iFamily != nFamilies; ++iFamily, pF = pF->m_Next.get() )
            {
                int                         nPools      = 0;
                int                         nEntities   = 0;

                //
                // Count how many pools we have
                //
                if( false == isRead )
                {
                    for( auto pP = &pF->m_DefaultPool; pP; pP = pP->m_Next.get() )
                    {
                        nEntities += pP->Size();
                        nPools++;
                    }
                }

                //
                // Deal with the families
                //
                xecs::pool::family::guid    FamilyGuid = isRead ? xecs::pool::family::guid{} : pF->m_Guid;

                if( TextFile.Record( Error, "Family"
                ,   [&]( std::size_t, xcore::err& Error ) noexcept
                    {
                          (Error = TextFile.Field("Guid",       FamilyGuid.m_Value ))
                        ||(Error = TextFile.Field("nPools",     nPools))
                        ||(Error = TextFile.Field("nEntities",  nEntities));
                    }
                )) return Error;

                if( nShareTypes && TextFile.Record( Error, "FamilyDetails"
                ,   [&]( std::size_t& C, xcore::err& ) noexcept
                    {
                        if( false == isRead ) C = pF->m_ShareInfos.size();
                        else                  assert( C == nShareTypes );
                    }
                ,   [&]( std::size_t i, xcore::err& Error ) noexcept
                    {
                        if( isRead )
                        {
                              (Error = TextFile.Field("Entity",     ShareEntities[i].m_Value ))
                            ||(Error = TextFile.Field("ShareKey",   ShareKeys[i].m_Value));
                        }
                        else
                        {
                              (Error = TextFile.Field("Entity",     pF->m_ShareDetails[i].m_Entity.m_Value ))
                            ||(Error = TextFile.Field("ShareKey",   pF->m_ShareDetails[i].m_Key.m_Value));
                        }
                    }
                )) return Error;

                // Create a family in case of reading
                if(isRead)
                {
                    // TODO: Make sure the order of families match the saved order in memory
                    //      To do that we need to fix the link list of pending families
                    pF = &pArchetype->CreateNewPoolFamily
                    ( FamilyGuid
                    , std::span{ ShareEntities.data(),  static_cast<std::size_t>(nShareTypes) }
                    , std::span{ ShareKeys.data(),      static_cast<std::size_t>(nShareTypes) }
                    );

                    //
                    // If the Family has shares then we must insert the shares into the hash map
                    //
                    if(nShareTypes)
                    {
                        for (int i = 0; i < nShareTypes; i++)
                        {
                            auto& Details = pF->m_ShareDetails[i];
                            m_ArchetypeMgr.m_ShareComponentEntityMap.emplace( std::pair{ Details.m_Key, Details.m_Entity });
                        }
                    }
                }

                //
                // Serialize Pools
                //
                auto pP = &pF->m_DefaultPool;
                for( int iPool =0; iPool < nPools; ++iPool, pP = pP->m_Next.get() )
                {
                    int     nEntitiesInPool = isRead ? 0 : pP->Size();

                    if( TextFile.Record( Error, "PoolInfo"
                    ,   [&]( std::size_t i, xcore::err& Error ) noexcept
                        {
                            Error = TextFile.Field("nEntities", nEntitiesInPool);
                        }
                    )) return Error;

                    // Do we have any entities that we need to deal with?
                    if( nEntitiesInPool == 0 )
                    {
                        //
                        // If we need to add another pool lets do so
                        //
                        if( isRead )
                        {
                            if ((iPool + 1) != nPools)
                            {
                                pP->m_Next = std::make_unique<pool::instance>();
                                pP->m_Next->Initialize(pF->m_DefaultPool.m_ComponentInfos, *pF);
                            }
                        }

                        // Go next pool
                        continue;
                    }

                    //
                    // Serialize Entities
                    //
                    if( isRead )
                    {
                        pP->Append( nEntitiesInPool );

                        for( auto iType = 0, end = (int)pP->m_ComponentInfos.size(); iType != end; iType++ )
                        {
                            if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER && pP->m_ComponentInfos[iType]->m_pSerilizeFn)
                            {
                                int Count = 0;
                                Error = pP->m_ComponentInfos[iType]->m_pSerilizeFn(TextFile, isRead, pP->m_pComponent[iType], Count);
                                if (Error) return Error;
                                assert(Count == nEntitiesInPool);
                            }
                            else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY && pP->m_ComponentInfos[iType]->m_pPropertyTable )
                            {
                                int             Count    = nEntitiesInPool;
                                std::byte*      pData    = pP->m_pComponent[iType];
                                auto            TypeSize = pP->m_ComponentInfos[iType]->m_Size;
                                auto&           Table    = *pP->m_ComponentInfos[iType]->m_pPropertyTable;
                                property::entry PropEntry;
                                xcore::crc<32>  CRCType;

                                for( int i = 0; i < Count; ++i )
                                {
                                    if( TextFile.Record
                                    (   Error
                                        , "Props"
                                        , [&](std::size_t& C, xcore::err&) noexcept
                                        {
                                            // No need to care about how many properties we need to read...
                                        }
                                        , [&](std::size_t, xcore::err& Err) noexcept
                                        {
                                            if( Err = TextFile.Field( "Name",     PropEntry.first ); Err ) return;

                                            TextFile.ReadFieldUserType( CRCType, "Data:?" ).clear();
                                            switch(CRCType.m_Value)
                                            {
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<int,             property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<int>()     ); Err) return; 
                                                    break;
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<float,           property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<float>()   ); Err) return; 
                                                    break;
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<bool,            property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<bool>()    ); Err) return; 
                                                    break;
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<string_t,        property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<string_t>()); Err) return; 
                                                    break;
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<xcore::vector2,  property::settings::data_variant>].m_CRC.m_Value:
                                                {
                                                    auto& V2 = PropEntry.second.emplace<xcore::vector2>();
                                                    if( Err = TextFile.Field("Data:?", V2.m_X, V2.m_Y ); Err ) return; break;
                                                    break;
                                                }
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<xecs::component::entity, property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<xecs::component::entity>().m_Value ); Err) return; 
                                                    break;
                                                case textfile_property_types_v[xcore::types::variant_t2i_v<std::int16_t,            property::settings::data_variant>].m_CRC.m_Value:
                                                    if( Err = TextFile.Field("Data:?", PropEntry.second.emplace<std::int16_t>() ); Err) return; 
                                                    break;
                                                default: assert(false);
                                            }
                                            static_assert(std::variant_size_v<property::settings::data_variant> == 7, "You need to change code inside this function ");

                                            if( false == property::set( Table, pData, PropEntry.first.c_str(), PropEntry.second ) )
                                            {
                                                printf("Warning failt to set a property while loading: %s", PropEntry.first.c_str() );
                                            }
                                            pData += TypeSize;
                                        }
                                    ); Error )
                                    {
                                        if( Error.getCode().getState<xcore::textfile::error_state>() == xcore::textfile::error_state::UNEXPECTED_RECORD )
                                        {
                                            printf( "Warning: We were expecting a table but we failed to find it");
                                            Error.clear();
                                        }
                                        else
                                        {
                                            return Error;
                                        }
                                    }
                                } // for
                            }
                            else
                            {
                                if( SerializedModes[iType] != details::serialized_mode::MODE_NONE )
                                {
                                    if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER )
                                    {
                                        return xerr_failure_s("Error: TextFile should have a skip record function, but none the less we failed to load file because we don't have serialized functions any more");
                                    }
                                    else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY )
                                    {
                                        //TextFile.SkipRecord();
                                        return xerr_failure_s("Error: TextFile should have a skip record function, but none the less we failed to load file because we don't have properties any more");
                                    }
                                    else
                                    {
                                        assert(false);
                                    }
                                }

                                continue;
                            }
        
                            // Are we dealing with the entities?
                            if( iType == 0 )
                            {
                                //
                                // Hook up with the global entries
                                //
                                for( int i=0; i<nEntitiesInPool; i++ )
                                {
                                    auto& Entity = reinterpret_cast<xecs::component::entity*>(pP->m_pComponent[0])[i];
                                    auto& Global = m_ComponentMgr.m_lEntities[Entity.m_GlobalIndex];

                                    assert( Global.m_Validation == Entity.m_Validation );
                                    Global.m_PoolIndex  = xecs::pool::index{i};
                                    Global.m_pPool      = pP;
                                }
                            }
                        }

                        //
                        // Add to the pending list
                        //
                        m_ArchetypeMgr.AddToStructuralPendingList(*pP);

                        //
                        // If we need to add another pool lets do so
                        //
                        if((iPool+1) != nPools)
                        {
                            pP->m_Next = std::make_unique<pool::instance>();
                            pP->m_Next->Initialize(pF->m_DefaultPool.m_ComponentInfos, *pF);
                        }
                    }
                    else
                    {
                        for( auto iType=0, end = (int)pP->m_ComponentInfos.size(); iType != end; iType++ )
                        {
                            auto& PropInfo = *pP->m_ComponentInfos[iType];
                            if( SerializedModes[iType] == details::serialized_mode::MODE_SERIALIZER )
                            {
                                int Count = pP->Size();
                                Error = pP->m_ComponentInfos[iType]->m_pSerilizeFn( TextFile, isRead, pP->m_pComponent[iType], Count );
                                if(Error) return Error;
                            }
                            else if( SerializedModes[iType] == details::serialized_mode::MODE_PROPERTY )
                            {
                                int         Count    = pP->Size();
                                std::byte*  pData    = pP->m_pComponent[iType];
                                auto        TypeSize = PropInfo.m_Size;
                                auto&       Table    = *PropInfo.m_pPropertyTable;

                                std::vector<property::entry> PropertyList;
                                for( int i=0; i<Count; ++i )
                                {
                                    // Collect all the properties for a single component
                                    PropertyList.clear();
                                    property::SerializeEnum( Table, pData + TypeSize * i, [&]( std::string_view PropertyName, property::data&& Data, const property::table&, std::size_t, property::flags::type Flags )
                                    {
                                        // If we are dealing with a scope that is not an array someone may have change the SerializeEnum to a DisplayEnum they only show up there.
                                        assert( Flags.m_isScope == false || PropertyName.back() == ']' );
                                        if( Flags.m_isDontSave || Flags.m_isScope ) return;
                                        PropertyList.push_back( property::entry { PropertyName, Data } );
                                    });

                                    // Serialize the properties for the give component
                                    TextFile.Record
                                    (   Error
                                        , "Props"
                                        , [&](std::size_t& C, xcore::err&) noexcept
                                        {
                                            assert(isRead == false);
                                            C = PropertyList.size();
                                        }
                                        , [&]( std::size_t c, xcore::err& Error ) noexcept
                                        {
                                            auto&       Entry       = PropertyList[c];

                                            Error = SerializeComponentWithProperties( isRead, TextFile, Entry.first, Entry.second );

                                            /*
                                            if( auto Err = TextFile.Field( "Name", Entry.first ); Err ) return;

                                            std::visit( [&]( auto&& Value )
                                            {
                                                using           T           = std::decay_t<decltype( Value )>;
                                                constexpr auto  prop_type_v = xcore::types::variant_t2i_v<T, property::settings::data_variant>;
                                                constexpr auto  CRC         = textfile_property_types_v[prop_type_v].m_CRC;

                                                if constexpr ( std::is_same_v<T, int> )
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value ); Err) return;
                                                }
                                                else if constexpr ( std::is_same_v<T, float> )
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value); Err) return;
                                                }
                                                else if constexpr ( std::is_same_v<T, bool> )
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value); Err) return;
                                                }
                                                else if constexpr ( std::is_same_v<T, string_t> )
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value); Err) return;
                                                }
                                                else if constexpr ( std::is_same_v<T, xcore::vector2> )
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value.m_X, Value.m_Y ); Err) return;
                                                }
                                                else if constexpr (std::is_same_v<T, xecs::component::entity>)
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value.m_Value); Err) return;
                                                }
                                                else if constexpr (std::is_same_v<T, std::int16_t>)
                                                {
                                                    if (auto Err = TextFile.Field(CRC, "Data:?", Value ); Err) return;
                                                }
                                                else static_assert( always_false<T>::value, "We are not covering all the cases!" );
                                            }
                                            , Entry.second );
                                            */
                                        }
                                    );
                                }
                            }
                            else
                            {
                                assert( SerializedModes[iType] == details::serialized_mode::MODE_NONE );
                                continue;
                            }
                        }
                    }
                }
            }
        }

        //
        // Fill unused global entities to the empty list
        //
        if( isRead )
        {
            m_ComponentMgr.m_EmptyHead = -1;
            auto Span = std::span{ m_ComponentMgr.m_lEntities.get(), xecs::settings::max_entities_v };
            for( auto It = Span.rbegin(); It != Span.rend(); ++It )
            {
                auto& E = *It;
                if( E.m_pPool == nullptr )
                {
                    E.m_PoolIndex.m_Value = m_ComponentMgr.m_EmptyHead;
                    m_ComponentMgr.m_EmptyHead = static_cast<int>(static_cast<std::size_t>(&E - m_ComponentMgr.m_lEntities.get()));
                }
            }

            //
            // Lets Update the structural changes
            //
            m_ArchetypeMgr.UpdateStructuralChanges();
        }

        return Error;
    }

    //---------------------------------------------------------------------------

    void instance::EditorSetEntityComponentProperty
    ( xecs::component::entity      Entity
    , xecs::component::type::guid  TypeGuid
    , property::entry              PropertyData
    , const bool                   isOverride
    ) noexcept
    {
        const auto pTypeInfo = m_ComponentMgr.findComponentTypeInfo(TypeGuid);
        if( pTypeInfo == nullptr )
        {
            XLOG_CHANNEL_WARNING( m_LogChannel, "You are trying to set a component's property, but the component is not register.");
            return;
        }
        assert(pTypeInfo->m_pPropertyTable);

        const auto& Details = m_ComponentMgr.getEntityDetails(Entity);
        if( Entity.m_Validation != Details.m_Validation ) 
        {
            XLOG_CHANNEL_WARNING(m_LogChannel, "You are trying to set an entity component's property, but the entity does not exist.");
            return;
        }

        const auto Bits  = Details.m_pPool->m_pArchetype->getComponentBits();
        if ( Bits.getBit(pTypeInfo->m_BitID) == false )
        {
            // This entity does not have the requested component
            return;
        }

        //
        // if we are dealing with an prefab Instance
        //
        if( Bits.getBit(xecs::component::type::info_v<xecs::prefab::instance>.m_BitID) )
        {
            auto& PrefabInstance = Details.m_pPool->getComponent<xecs::prefab::instance>(Details.m_PoolIndex);

            // Was this property previously overwritten?
            bool bAlreadyOverriten = false;
            for (int i = 0; i < PrefabInstance.m_lComponents.size(); ++i)
            {
                auto& CompInfo = PrefabInstance.m_lComponents[i];
                if( CompInfo.m_ComponentTypeGuid == TypeGuid )
                {
                    for( auto& E : CompInfo.m_PropertyOverrides )
                    {
                        if( E.m_PropertyName == PropertyData.first )
                        {
                            bAlreadyOverriten = true;
                            break;
                        }
                    }
                    break;
                }
            }

            if( isOverride )
            {
                // add the override officially
                if( bAlreadyOverriten == false )
                {
                    PrefabInstance.m_lComponents.emplace_back
                    ( xecs::prefab::instance::component
                    { .m_ComponentTypeGuid  = TypeGuid
                    , .m_PropertyOverrides  = std::vector{ xecs::prefab::instance::component::override{PropertyData.first, textfile_property_types_v[ textfile_porperty_data_to_index(PropertyData.second) ].m_CRC} }
                    , .m_Type               = xecs::prefab::instance::component::type::OVERRIDES
                    });
                }
            }
            else
            {
                // If we are not overwriting and we have overrode it in the past then there is nothing to do
                if( bAlreadyOverriten == true )
                    return;
            }
        }

        //
        // Set the property
        //
        auto Index = Bits.getIndexOfComponent(pTypeInfo->m_BitID);
        if( pTypeInfo->m_TypeID == xecs::component::type::id::DATA )
        {
            property::set( *pTypeInfo->m_pPropertyTable, &Details.m_pPool->m_pComponent[Index][Details.m_PoolIndex.m_Value * pTypeInfo->m_Size], PropertyData.first.c_str(), PropertyData.second );
        }
        else
        {
            assert(pTypeInfo->m_TypeID == xecs::component::type::id::SHARE );
            Index -= Details.m_pPool->m_pArchetype->m_nDataComponents;

            auto& Family = *Details.m_pPool->m_pMyFamily;

            assert( Family.m_ShareInfos[Index]->m_Guid == TypeGuid );


            auto& ShareComponentDetails = m_ComponentMgr.getEntityDetails(Family.m_ShareDetails[Index].m_Entity);
            void* pComponent            = nullptr;

            for( int i=1, end = (int)ShareComponentDetails.m_pPool->m_ComponentInfos.size(); i<end; ++i )
            {
                if(ShareComponentDetails.m_pPool->m_ComponentInfos[i]->m_Guid == TypeGuid)
                {
                    pComponent = &ShareComponentDetails.m_pPool->m_pComponent[i][ ShareComponentDetails.m_PoolIndex.m_Value * ShareComponentDetails.m_pPool->m_ComponentInfos[i]->m_Size ];
                    break;
                }
            }
            assert(pComponent);

            // Copy the data to temporary buffer
            auto pData = xcore::memory::AlignedMalloc( xcore::units::bytes{ pTypeInfo->m_Size}, 16 );
            if( pTypeInfo->m_pCopyFn )
            {
                pTypeInfo->m_pCopyFn( reinterpret_cast<std::byte*>(pData), reinterpret_cast<std::byte*>(pComponent) );
            }
            else
            {
                std::memcpy(pData, pComponent, pTypeInfo->m_Size);
            }

            // Set the property
            property::set(*pTypeInfo->m_pPropertyTable, pData, PropertyData.first.c_str(), PropertyData.second);

            auto Key = xecs::component::type::details::ComputeShareKey( Details.m_pPool->m_pArchetype->getGuid(), *pTypeInfo, reinterpret_cast<std::byte*>(pData) );

            //
            // get the new family if we have to
            //
            if( Key != Family.m_ShareDetails[Index].m_Key )
            {
                xecs::tools::bits Bits;

                Bits.setBit(pTypeInfo->m_BitID);
                Details.m_pPool->m_pArchetype->getOrCreatePoolFamilyFromSameArchetype
                ( Family
                , Bits
                , { reinterpret_cast<std::byte**>(&pData), 1ull }
                , { &Key, 1ull }
                ).MoveIn
                ( *this
                , Family
                , *Details.m_pPool
                , Details.m_PoolIndex
                );
            }

            //
            // Free the temp memory
            //
            xcore::memory::AlignedFree(pData);
        }

        //
        // Check if our entity is a prefab if so we must update every instance and update it
        //
        if( Bits.getBit( xecs::component::type::info_v<xecs::prefab::tag>.m_BitID ) == false ) return;

        //
        // First we need to update Variants before regular entities
        //
        {
            xecs::query::instance Query;
            Query.m_Must.AddFromComponents<xecs::prefab::instance, xecs::prefab::tag>();

            auto S = Search(Query);
            Foreach( S, [&]( xecs::component::entity& VariantEntity, xecs::prefab::instance& Instance ) constexpr noexcept
            {
                if( Instance.m_PrefabEntity == Entity )
                {
                    EditorSetEntityComponentProperty( VariantEntity, TypeGuid, PropertyData, false );
                }
            });
        }

        //
        // Now lets update all the entity instances
        //
        {
            xecs::query::instance Query;
            Query.m_Must.AddFromComponents<xecs::prefab::instance>();
            Query.m_NoneOf.AddFromComponents<xecs::prefab::tag>();

            auto S = Search(Query);
            Foreach(S, [&]( xecs::component::entity& RegularEntity, xecs::prefab::instance& Instance) constexpr noexcept
            {
                if( Instance.m_PrefabEntity == Entity )
                {
                    EditorSetEntityComponentProperty( RegularEntity, TypeGuid, PropertyData, false);
                }
            });
        }
    }

    //---------------------------------------------------------------------------
    
    xcore::err instance::SerializePrefabs( const char* pFolderName, bool isRead, bool isBinary ) noexcept
    {
        xcore::err                                          Error;
        std::vector< std::vector<xecs::component::entity> > ListOfPrefabs;

        //
        // Make sure that we are all up to date
        //
        if (isRead == false) m_ArchetypeMgr.UpdateStructuralChanges();

        //
        // Collect all the prefabs (not variants)
        //
        {
            xecs::query::instance Query;

            Query.m_Must.AddFromComponents<xecs::prefab::tag>();
            Query.m_NoneOf.AddFromComponents<xecs::prefab::instance>();

            Foreach( Search(Query), [&]( const xecs::component::entity& Entity ) constexpr noexcept
            {
                if( ListOfPrefabs.size() == 0 )
                    ListOfPrefabs.emplace_back( std::vector<xecs::component::entity>{ Entity } );
                else
                    ListOfPrefabs[0].emplace_back( Entity );
            });
        }

        //
        // Collect all the variants and put them using the right depth
        //
        if( ListOfPrefabs.size() )
        {
            xecs::query::instance Query;
            Query.m_Must.AddFromComponents<xecs::prefab::tag, xecs::prefab::instance>();

            const std::function<int(xecs::component::entity, int)> DetermineDepth =
            [&]( xecs::component::entity Entity, int Count ) constexpr noexcept
            {
                if( getArchetype(Entity).getComponentBits().getBit( xecs::component::type::info_v<xecs::prefab::instance>.m_BitID ) == false )
                {
                    (void)getEntity( Entity, [&](xecs::prefab::instance& Instance ) noexcept
                    {
                        Count = DetermineDepth( Instance.m_PrefabEntity, Count+1 );
                    });
                }

                return Count;
            };

            Foreach( Search(Query), [&]( const xecs::component::entity& Entity ) noexcept
            {
                const auto Depth = DetermineDepth( Entity, 1 );

                ListOfPrefabs.resize( std::max( Depth + 1, static_cast<int>(ListOfPrefabs.size() ) ) );

                ListOfPrefabs[Depth].emplace_back( Entity );
            });
        }

        //
        // Serialize prefabs out
        // Each prefab must be saved in a different file
        //        
        if( false == std::filesystem::create_directory( pFolderName ) )
        {
            // Some error creating the directory??
        }
        for(const auto& EL : ListOfPrefabs )
        for( const auto& Entity  : EL )
        {
            const auto&             Archetype       = getArchetype(Entity);
            const int               iLevel          = static_cast<int>(static_cast<std::size_t>(&EL - &ListOfPrefabs[0]));
            xcore::textfile::stream Stream;

            //
            // Open file
            //
            {
                xcore::guid::unit<64> Code{ Entity.m_Value };

                std::string FileName = xcore::string::Fmt("%s/%s.%d.prefab", pFolderName, xcore::string::IntToCompactFileName<char>(Entity.m_Value).data(), iLevel).data();
                if( Error = Stream.Open(false, FileName, isBinary ? xcore::textfile::file_type::BINARY : xcore::textfile::file_type::TEXT))
                {
                    //TODO: Report and return the Error
                    return Error;
                }

                // Add the property types
                Stream.AddUserTypes(textfile_property_types_v);
            }

            //
            // Are we a variant?
            //
            if( Archetype.getComponentBits().getBit(xecs::component::type::info_v<xecs::prefab::instance>.m_BitID) )
            {
                //-----------------------------------------------------------------------
                // Serialize out Variants only here
                //-----------------------------------------------------------------------
                (void)getEntity( Entity, [&](xecs::prefab::instance& Instance ) noexcept
                {
                    // Save the basic info... (Entity, Prefab)
                    const int nNewComponents = [&] { int n = 0; for (const auto& E : Instance.m_lComponents) if (E.m_Type == xecs::prefab::instance::component::type::NEW) n++; return n; }();

                    if( Stream.Record
                    ( Error
                    , "PrefabVariant"
                    , [&](std::size_t, xcore::err& Err ) noexcept
                    {
                           std::uint64_t v = Entity.m_Value;
                           int           n = nNewComponents;
                           (Err = Stream.Field( "EntityID",       v ))
                        || (Err = Stream.Field( "nNewComponents", n ))
                        ;
                    })) return;

                    //
                    // If we don't have anything else to serialize call it quits
                    //
                    if(Instance.m_lComponents.size() == 0 ) return;

                    //
                    // Lets serialize the rest
                    //
                    int nOverrides = 0;

                    if( Stream.Record
                    ( Error
                    , "Components"
                    , [&]( std::size_t& Count, xcore::err& Err ) noexcept
                    {
                        Count = Instance.m_lComponents.size();
                    }
                    , [&]( std::size_t Index, xcore::err& Err ) noexcept
                    {
                        auto& Com = Instance.m_lComponents[Index];

                        // get the type
                        int C = [&]() noexcept
                        {
                            switch (Com.m_Type)
                            {
                            case xecs::prefab::instance::component::type::NEW:       return 0;
                            case xecs::prefab::instance::component::type::REMOVE:    return 2;
                            case xecs::prefab::instance::component::type::OVERRIDES: return 1;
                            }
                            xassert(false);
                            return 0;
                        }();

                        // Keep track on overrites 
                        int n = static_cast<int>(Com.m_PropertyOverrides.size());
                        nOverrides += n;

                            (Err = Stream.Field( "ComponentGuid", Com.m_ComponentTypeGuid.m_Value ))
                         || (Err = Stream.Field( "Type",          C ) )
                         || (Err = Stream.Field( "nOverrides",    n ) )
                         ;
                    })) return;

                    const auto& Details = m_ComponentMgr.getEntityDetails(Entity);
                    const auto& Bits    = getArchetype(Entity).getComponentBits();

                    //
                    // Serialize all new componets fully
                    //
                    if( nNewComponents )
                    {
                        for ( auto& E: Instance.m_lComponents )
                        {
                            if (E.m_Type != xecs::prefab::instance::component::type::NEW)
                                continue;

                            const auto& Info = *m_ComponentMgr.findComponentTypeInfo(E.m_ComponentTypeGuid);
                            if( Error = SerializeComponentWithProperties
                            ( isRead
                            , Stream
                            , Details
                            , Info
                            , Details.m_pPool->m_pComponent[ Details.m_pPool->m_pArchetype->getComponentBits().getIndexOfComponent(Info.m_BitID) ]
                            ) ) return;
                        }
                    }

                    //
                    // Serialize overrides
                    //
                    if(nOverrides)
                    {
                        int iCom=0;
                        int iOver=0;

                        (void)Stream.Record
                        ( "Overrides"
                        , [&]( std::size_t& Count, xcore::err& Err ) constexpr noexcept
                        {
                            Count = nOverrides;
                        }
                        , [&]( std::size_t Index, xcore::err& Err ) mutable noexcept
                        {
                            if( iOver >= Instance.m_lComponents[iCom].m_PropertyOverrides.size() )
                            {
                                iOver = 0;
                                iCom++;
                            }
                            while( Instance.m_lComponents[iCom].m_Type != xecs::prefab::instance::component::type::OVERRIDES )
                            {
                                iCom++;
                                iOver = 0;
                            }

                            auto& Com = Instance.m_lComponents[iCom];
                            Stream.Field( "ComponentGuid", Com.m_ComponentTypeGuid.m_Value ).clear();

                            //
                            // Serialize the property data
                            //
                            auto& Entry   = Com.m_PropertyOverrides[iOver];
                            auto& Info    = *m_ComponentMgr.findComponentTypeInfo(Com.m_ComponentTypeGuid);
                            auto  Data    = xcore::property::get( *Info.m_pPropertyTable, Details.m_pPool->m_pComponent[ Bits.getIndexOfComponent(Info.m_BitID) ] + Details.m_PoolIndex.m_Value * Info.m_Size, Entry.m_PropertyName.c_str() );

                            Err = SerializeComponentWithProperties( isRead, Stream, Entry.m_PropertyName, Data );
                        });

                        if( Error ) return;
                    }

                });// end of get entity
            } // if end of are we a variant
            else
            {
                //-----------------------------------------------------------------------
                // Serialize out Prefabs only here
                //-----------------------------------------------------------------------

                //
                // Read the archetype types
                //
                std::vector<const xecs::component::type::info*> ArchetypeInfos;
                Archetype.getComponentBits().Foreach( [&]( int, auto& Info ) noexcept 
                {
                    ArchetypeInfos.push_back(&Info);
                });
                std::array<std::int8_t, xecs::settings::max_components_per_entity_v> SerializedModes{};

                if( Stream.Record( Error, "Archetype"
                    ,   [&]( std::size_t& C, xcore::err& ) noexcept
                        {
                            C = ArchetypeInfos.size();
                        }
                    ,   [&]( std::size_t i, xcore::err& Error ) noexcept
                        {
                            xecs::component::type::guid Guid
                                = isRead
                                ? xecs::component::type::guid{}
                                : ArchetypeInfos[i]->m_Guid;
                            
                            if( (Error = Stream.Field( "TypeGuid", Guid.m_Value )) ) return;

                            if( isRead )
                            {
                                auto pInfo = m_ComponentMgr.findComponentTypeInfo(Guid);
                                if( pInfo == nullptr )
                                {
                                    // TODO: Change this to a warning?
                                    Error = xerr_failure_s("Serialization Error, Fail to find one of the components types");
                                    return;
                                }

                                // Set the info into the structure this includes tags
                                ArchetypeInfos[i] = pInfo;
                            }

                            // We need to know how this type was serialized when loading. This is only relevant when
                            // we saved with properties and then we add the serializing function, for the loader to do the
                            // right thing it should used the properties loader since this is how it was saved with.
                            if( isRead == false )
                            {
                                if(ArchetypeInfos[i]->m_pSerilizeFn         )  SerializedModes[i] = details::serialized_mode::MODE_SERIALIZER;
                                else if (ArchetypeInfos[i]->m_pPropertyTable ) SerializedModes[i] = details::serialized_mode::MODE_PROPERTY;
                                else                                           SerializedModes[i] = details::serialized_mode::MODE_NONE;
                            }
                            Stream.Field("SerializationMode", SerializedModes[i]).clear();
                        }
                    )) return Error;

                //
                // Remove all the tags from the ArchetypeInfo
                //
                {
                    const auto nTags = ArchetypeInfos.size() - Archetype.m_nShareComponents - Archetype.m_nDataComponents;
                    ArchetypeInfos.resize(ArchetypeInfos.size() - nTags);
                }

                //
                // Serialize share components
                //
                const auto& Details = m_ComponentMgr.getEntityDetails(Entity);
                if( Archetype.m_nShareComponents )
                {
                    // Loop throw all the share componet types
                    int Index = 0;
                    for( const auto& pInfo : std::span{ ArchetypeInfos.data() + Archetype.m_nDataComponents, ArchetypeInfos.size() - Archetype.m_nDataComponents } )
                    {
                        xassert( Details.m_pPool->m_pMyFamily->m_ShareInfos[Index] == pInfo );
                        
                        auto&       ShareDetails    = m_ComponentMgr.getEntityDetails( Details.m_pPool->m_pMyFamily->m_ShareDetails[Index].m_Entity );
                        const auto  idx             = ShareDetails.m_pPool->findIndexComponentFromInfo( *pInfo ); // Find the index of the component
                        auto        pData           = ShareDetails.m_pPool->m_pComponent[idx] + ShareDetails.m_PoolIndex.m_Value * pInfo->m_Size;

                        if( SerializedModes[Index] == details::serialized_mode::MODE_SERIALIZER)
                        {
                            int Count = 1;                            
                            auto Error = pInfo->m_pSerilizeFn(Stream, isRead, pData, Count);
                            if (Error) return Error;
                        }
                        else if( SerializedModes[Index] == details::serialized_mode::MODE_PROPERTY)
                        {
                            auto Error = SerializeComponentWithProperties( isRead, Stream, ShareDetails, *pInfo, pData );
                            if (Error) return Error;
                        }
                        else
                        {
                            // Nothing should be making it to here
                            xassert(false);
                        }

                        Index++;
                    }
                }

                //
                // Serialize data components
                //
                int Index = 0;
                for( const auto& pInfo : std::span{ ArchetypeInfos.data(), Archetype.m_nDataComponents } )
                {
                    xassert(Details.m_pPool->m_ComponentInfos[Index] == pInfo);

                    auto pData = Details.m_pPool->m_pComponent[Index] + Details.m_PoolIndex.m_Value * pInfo->m_Size;

                    if( SerializedModes[Index] == details::serialized_mode::MODE_SERIALIZER )
                    {
                        int Count = 1;
                        Error = pInfo->m_pSerilizeFn( Stream, isRead, pData, Count );
                        if (Error) return Error;
                    }
                    else if( SerializedModes[Index] == details::serialized_mode::MODE_PROPERTY )
                    {
                        Error = SerializeComponentWithProperties( isRead, Stream, Details, *pInfo, pData );
                        if (Error) return Error;
                    }

                    Index++;
                }
            }
        } // end of loop

        return Error;
    }


    //----------------------------------------------------------------------------------------------

    xcore::err instance::SerializeGameStateV2
    ( const char* pFileName
    , bool        isRead
    , bool        isBinary
    ) noexcept
    {
        xcore::textfile::stream     TextFile;
        xcore::err                  Error;
#if 0
        //
        // Have the file system know the mapping of the property types
        //
        TextFile.AddUserTypes(textfile_property_types_v);

        //
        // Make sure that we are all up to date
        //
        if (isRead == false) m_ArchetypeMgr.UpdateStructuralChanges();

        //
        // Open file for writing
        //
        if (Error = TextFile.Open
        (isRead
            , std::string_view{ pFileName }
            , isBinary ? xcore::textfile::file_type::BINARY : xcore::textfile::file_type::TEXT
            , { xcore::textfile::flags::WRITE_FLOATS }
        ); Error) return Error;

        //
        // Version
        //
        if (TextFile.Record(Error, "Version"
            , [&](std::size_t i, xcore::err& Err ) noexcept
            {
                static constexpr xcore::string::constant<char> context_v{ "editor::xECS" };
                xcore::cstring Format;
                int            Major;
                int            Minor;
                
                if( false == isRead ) 
                {
                    Format = xcore::string::Fmt( context_v.data() );
                    Major  = 1;
                    Minor  = 0;
                }

                // Read/Write

                   (Err = TextFile.Field("Context", Format ))
                || (Err = TextFile.Field("Major",   Major  ))
                || (Err = TextFile.Field("Minor",   Minor  ));

                // Check version
                if( !Err )
                {
                    if( Format != context_v ) 
                    {
                        Err = xerr_failure_s( "Wrong Version of the file format" );
                    }
                }
            }
        )) return Error;

        //
        // Read/Write Global Entity Validations
        //
        {
            int Count = 0;
            if (false == isRead)
            {
                //
                // Determine the max index we want to write
                //
                auto Span = std::span{ m_ComponentMgr.m_lEntities.get(), xecs::settings::max_entities_v };
                for (auto It = Span.rbegin(); It != Span.rend(); ++It)
                {
                    auto& E = *It;
                    if (E.m_Validation.m_Value)
                    {
                        Count = 1 + static_cast<int>(static_cast<std::size_t>(&E - m_ComponentMgr.m_lEntities.get()));
                        break;
                    }
                }
            }

            if( TextFile.Record( Error, "GlobalEntities"
            ,   [&]( std::size_t& C, xcore::err& ) noexcept
                {
                    if (false == isRead) C     = Count;
                    else                 Count = static_cast<int>(C);
                }
            ,   [&]( std::size_t i, xcore::err& Error )
                {
                    Error = TextFile.Field("Validation", m_ComponentMgr.m_lEntities[i].m_Validation.m_Value );
                }
            ))
            {
                if( Error.getCode().getState<xcore::textfile::error_state>() != xcore::textfile::error_state::UNEXPECTED_RECORD )
                    return Error;
            }
        }

        //
        // Start saving entities
        //
        for( int iArchetype = 0; true; iArchetype++ )
        {
            std::array< details::component_serialized_mode, xecs::settings::max_components_per_entity_v > Components;
            int                                                                                           ComponentCount=0;

            if( isRead ) 
            {
                // Make sure we have something to read
                if (TextFile.isEOF()) break;
            }
            else
            {
                // Exclude archetypes that are mark as prefabs
                for( ; iArchetype < m_ArchetypeMgr.m_lArchetype.size(); iArchetype++)
                {
                    auto& E = m_ArchetypeMgr.m_lArchetype[iArchetype]->getComponentBits();
                    if( false == E.getBit(xecs::component::type::info_v<xecs::prefab::tag>.m_BitID) )
                    {
                        if( false == E.getBit(xecs::component::type::info_v<xecs::component::share_as_data_exclusive_tag>.m_BitID) ) break;
                        m_ArchetypeMgr.m_lArchetype[iArchetype]->m_ComponentBits.Foreach( [&]( int, auto& Info ) noexcept
                        {
                            if( Info.m_TypeID == xecs::component::type::id::SHARE )
                            {
                              //  if( Info.m_bGlobalScoped == false )
                            }
                        });
                    }
                }

                if(iArchetype == m_ArchetypeMgr.m_lArchetype.size() ) break;

                // Write a bit of information for the user
                if ((Error = TextFile.WriteComment(" Component Details: "))) return Error;
                m_ArchetypeMgr.m_lArchetype[iArchetype]->getComponentBits().Foreach( [&]( int, auto& Info ) noexcept
                {
                    if( (Error = TextFile.WriteComment( xcore::string::Fmt( "   Guid:%.16llX   Type:%s   Name:%s"
                        , Info.m_Guid.m_Value
                        , Info.m_TypeID == xecs::component::type::id::DATA 
                            ? "Data"
                            : Info.m_TypeID == xecs::component::type::id::SHARE
                            ? "Share"
                            : "Tag"
                        , Info.m_pName ).getView()))) return Error;

                    //
                    // Fill the table
                    //
                    Components[ComponentCount].m_Guid  = Info.m_Guid;
                    Components[ComponentCount].m_pInfo = &Info;
                    Components[ComponentCount].m_Mode  = Info.m_pSerilizeFn ? details::MODE_SERIALIZER
                                                                            : Info.m_pPropertyTable ? details::MODE_PROPERTY
                                                                            ? details::MODE_NONE;
                    ComponentCount++;
                });
            }

            if( TextFile.Record( Error, "Archetype"
            ,   [&]( std::size_t& C, xcore::err& ) noexcept
                {
                    if (isRead) ComponentCount = C; 
                    else        C = ComponentCount;
                }
            ,   [&]( std::size_t i, xcore::err& Err ) noexcept
                {
                       ( Err = TextFile.Field("TypeGuid",          Components[i].m_Guid ))
                    || ( Err = TextFile.Field("SerializationMode", Components[i].m_Mode ));
                }
            ))
            {
                if( Error.getCode().getState<xcore::textfile::error_state>() != xcore::textfile::error_state::UNEXPECTED_RECORD )
                    return Error;
            }

            //
            // Check how many components do we still have...
            // Since the user may have remove some components since last time we saved
            //
            if( false == isRead )
            {
                std::array< const xecs::component::type::info*, xecs::settings::max_components_per_entity_v > ComponentInfos;
                int                                                                                           InfoCount = 0;
                for( int i=0; i< ComponentCount; ++i )
                {
                    Components[i].m_pInfo = m_ComponentMgr.findComponentTypeInfo(Components[i].m_Guid);
                    if(Components[i].m_pInfo) ComponentInfos[InfoCount++] = Components[i].m_pInfo;
                }

                // Determine the real archetype
                auto pFinalArchetype = &getOrCreateArchetype( std::span{ ComponentInfos.data(), static_cast<std::size_t>(InfoCount) } );
            }


            else if (xcore::string::Compare(TextFile.getRecordName(), "ArchetypeTypes") == 0) if (auto Err = SerializeArchetype(*m_ArchetypeMgr.m_lArchetype[iArchetype]); Err) return Err;
            else if (xcore::string::Compare(TextFile.getRecordName(), "ArchetypeTypes") == 0) if (auto Err = SerializeArchetype(*m_ArchetypeMgr.m_lArchetype[iArchetype]); Err) return Err;
            else return xerr_failure_s("xECS failed to read file because it found an unexpected record");

        }
#endif

        return {};
    }

} // end of namespace
