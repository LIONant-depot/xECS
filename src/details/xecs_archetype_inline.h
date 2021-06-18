namespace xecs::archetype
{
    //--------------------------------------------------------------------------------------------

    instance::instance
    ( xecs::game_mgr::instance& GameMgr 
    ) noexcept
    : m_GameMgr{ GameMgr }
    {}

    //--------------------------------------------------------------------------------------------

    template< typename T_FUNCTION >
    requires
    ( xcore::function::is_callable_v<T_FUNCTION>
    && std::is_same_v<typename xcore::function::traits<T_FUNCTION>::return_type, void>
    && xcore::function::traits<T_FUNCTION>::arg_count_v == 0
    )
    void instance::AccessGuard( T_FUNCTION&& Function ) noexcept
    {
        ++m_ProcessReference;
        Function();
        if(--m_ProcessReference == 0 ) UpdateStructuralChanges();
    }

    //--------------------------------------------------------------------------------------------

    void instance::Initialize
    ( std::span<const xecs::component::info* const>  Infos
    , const tools::bits&                             Bits 
    ) noexcept
    {
        // Deep copy the infos just in case the user gave us data driven infos
        bool AreTheySorted = true;
        for( int i=0; i<Infos.size(); ++i )
        {
            m_InfoData[i] = Infos[i];
            if( i && m_InfoData[i-1] > m_InfoData[i] ) AreTheySorted = false;
        }

        // Short Infos base on their GUID (smaller first) entry 0 should be the entity
        if( false == AreTheySorted )
        {
            std::sort
            ( m_InfoData.begin()
            , m_InfoData.begin() + Infos.size()
            , []( const xecs::component::info* pA, const xecs::component::info* pB ) constexpr
            {
                return pA->m_Guid < pB->m_Guid;
            });
        }

        // We can initialize our default pool
        m_Pool.Initialize( { m_InfoData.data(), Infos.size() } );
        m_ComponentBits = Bits;
    }

    //--------------------------------------------------------------------------------------------

    template
    < typename T_CALLBACK
    > requires
    ( xcore::function::is_callable_v<T_CALLBACK>
   && std::is_same_v<typename xcore::function::traits<T_CALLBACK>::return_type, void>
    )
    xecs::component::entity instance::CreateEntity
    ( T_CALLBACK&& Function 
    ) noexcept
    {
        using func_traits = xcore::function::traits<T_CALLBACK>;
        return[&]< typename... T_COMPONENTS >(std::tuple<T_COMPONENTS...>*) constexpr noexcept
        {
            assert(m_ComponentBits.getBit(component::info_v<T_COMPONENTS>.m_UID) && ...);

            // Allocate the entity
            const int   EntityIndexInPool   = m_Pool.Append(1);
            const auto  Entity              = m_GameMgr.AllocNewEntity(EntityIndexInPool, *this);
            m_Pool.getComponent<xecs::component::entity>(EntityIndexInPool) = Entity;

            // Call the user initializer if any
            if constexpr (false == std::is_same_v<xecs::tools::empty_lambda, T_CALLBACK >)
                Function(m_Pool.getComponent<std::remove_reference_t<T_COMPONENTS>>(EntityIndexInPool) ...);

            return Entity;
        }( xcore::types::null_tuple_v<func_traits::args_tuple> );
    }

    //--------------------------------------------------------------------------------------------
    template
    < typename T_CALLBACK
    > requires
    ( xcore::function::is_callable_v<T_CALLBACK>
   && std::is_same_v<typename xcore::function::traits<T_CALLBACK>::return_type, void>
    )
    void instance::CreateEntities
    ( int               Count
    , T_CALLBACK&&      Function 
    ) noexcept
    {
        using func_traits = xcore::function::traits<T_CALLBACK>;

        // Allocate the entity
        if constexpr ( std::is_same_v<xecs::tools::empty_lambda, T_CALLBACK > )
        {
            for (int EntityIndexInPool = m_Pool.Append(Count)
                , end = EntityIndexInPool + Count; EntityIndexInPool < end; ++EntityIndexInPool)
            {
                m_Pool.getComponent<xecs::component::entity>(EntityIndexInPool)
                    = m_GameMgr.AllocNewEntity(EntityIndexInPool, *this);
            }
        }
        else
        {
            int EntityIndexInPool = m_Pool.Append(Count);
            std::array< std::byte*, func_traits::arg_count_v > CachePointers;
            {
                using sorted_tuple = xecs::component::details::sort_tuple_t<func_traits::args_tuple>;
                int Sequence = 0;
                [&] <typename... T >(std::tuple<T...>*) constexpr noexcept
                {
                    assert(m_ComponentBits.getBit(component::info_v<T>.m_UID) && ...);
                    static_assert( ((std::is_reference_v<T> ) && ... ) );
                    ((CachePointers[xcore::types::tuple_t2i_v< T, func_traits::args_tuple>] = [&]<typename T_C>(std::tuple<T_C>*) constexpr noexcept
                    {
                        return &m_Pool.m_pComponent[m_Pool.findIndexComponentFromGUIDInSequence(xecs::component::info_v<T_C>.m_Guid, Sequence)][sizeof(T_C) * EntityIndexInPool];
                    }(xcore::types::make_null_tuple_v<T>)), ...);
                }(xcore::types::null_tuple_v<sorted_tuple>);
            }

            xecs::component::entity* pEntity = &reinterpret_cast<xecs::component::entity*>(m_Pool.m_pComponent[0])[EntityIndexInPool];
            for( ; Count; --Count )
            {
                // Fill the entity data
                *pEntity = m_GameMgr.AllocNewEntity(EntityIndexInPool, *this);
                pEntity++;

                // Call the user initializer
                [&]<typename...T>(std::tuple<T...>*) constexpr noexcept
                {
                    Function( reinterpret_cast<T>( *CachePointers[xcore::types::tuple_t2i_v<T, func_traits::args_tuple>] )
                            ... );
                    ((CachePointers[xcore::types::tuple_t2i_v<T, func_traits::args_tuple>] += sizeof(std::remove_reference_t<T>)), ... );
                }( xcore::types::null_tuple_v<func_traits::args_tuple> );
            }
        }
    }

    //--------------------------------------------------------------------------------------------

    void instance::DestroyEntity
    ( xecs::component::entity& Entity 
    ) noexcept
    {
        assert( Entity.isZombie() == false );

        //
        // Validate the crap out of the entity
        //
        auto& GlobalEntry = m_GameMgr.m_lEntities[Entity.m_GlobalIndex];

        if(Entity.m_Validation != GlobalEntry.m_Validation
        || GlobalEntry.m_pArchetype != this ) return;
        
        // Make sure that we are in sync with the pool entity
        auto& PoolEntity = m_Pool.getComponent<xecs::component::entity>(GlobalEntry.m_PoolIndex);
        if( PoolEntity != Entity )
            return;

        //
        // Check if we can delete it right away
        //
        if (0 == m_ProcessReference)
        {
            Entity.m_Validation.m_bZombie = true;
            m_Pool.Delete(GlobalEntry.m_PoolIndex);
            if (GlobalEntry.m_PoolIndex != m_Pool.Size())
            {
                m_GameMgr.DeleteGlobalEntity(Entity.m_GlobalIndex, PoolEntity);
            }
            else
            {
                m_GameMgr.DeleteGlobalEntity(Entity.m_GlobalIndex);
            }
        }
        else
        {
            //
            // Make it into a zombie
            //
            Entity.m_Validation.m_bZombie
                = GlobalEntry.m_Validation.m_bZombie
                = PoolEntity.m_Validation.m_bZombie     // Just in case that Entity is a reference and not the real entity
                = true;

            //
            // Added into the delete link list
            //
            PoolEntity.m_Validation.m_Generation = m_DeleteGlobalIndex;
            m_DeleteGlobalIndex                  = PoolEntity.m_GlobalIndex;
        }
    }

    //--------------------------------------------------------------------------------------------

    void instance::UpdateStructuralChanges
    ( void
    ) noexcept
    {
        while( m_DeleteGlobalIndex != invalid_delete_global_index_v )
        {
            auto&       Entry                   = m_GameMgr.m_lEntities[m_DeleteGlobalIndex];
            auto&       PoolEntity              = m_Pool.getComponent<xecs::component::entity>(Entry.m_PoolIndex);
            const auto  NextDeleteGlobalIndex   = PoolEntity.m_Validation.m_Generation;
            assert(PoolEntity.m_Validation.m_bZombie);

            m_Pool.Delete(Entry.m_PoolIndex);
            if (Entry.m_PoolIndex != m_Pool.Size())
            {
                m_GameMgr.DeleteGlobalEntity(m_DeleteGlobalIndex, PoolEntity);
            }
            else
            {
                m_GameMgr.DeleteGlobalEntity(m_DeleteGlobalIndex);
            }

            m_DeleteGlobalIndex = NextDeleteGlobalIndex;
        }
    }
}
