namespace xecs::pool
{
    //-------------------------------------------------------------------------------------
    constexpr
    family::guid family::ComputeGuid
    ( const xecs::archetype::guid                           Guid
    , std::span<const xecs::component::type::info* const>   Types
    , std::span<const std::byte* const>                     Data 
    ) noexcept
    {
        assert( Types.size() == Data.size() );
        family::guid FinalGuid{ Guid.m_Value };
        for( int i=0; i<Data.size(); i++ )
        {
            FinalGuid.m_Value += xecs::component::type::details::ComputeShareKey( Guid, *Types[i], Data[i] ).m_Value;
        }
        return FinalGuid;
    }

    //-------------------------------------------------------------------------------------

    instance::~instance(void) noexcept
    {
        if (m_pComponent[0])
        {
            Clear();
            for (int i = 0, end = static_cast<int>(m_ComponentInfos.size()- m_ShareComponentCount); i < end; ++i)
            {
                VirtualFree(m_pComponent[i], 0, MEM_RELEASE);
            }
        }
    }

    //-------------------------------------------------------------------------------------
    // This function return which page is the last byte of a given entry
    //-------------------------------------------------------------------------------------
    constexpr inline 
    int getPageFromIndex( const component::type::info& Info, int iEntity ) noexcept
    {
        return ((iEntity * static_cast<int>(Info.m_Size))-1) / xecs::settings::virtual_page_size_v;
    }

    //-------------------------------------------------------------------------------------

    void instance::Initialize
    ( std::span<const component::type::info* const > Span
    , std::span<const xecs::component::entity      > ShareComponents
    ) noexcept
    {
        m_ComponentInfos            = Span;
        m_ShareComponentCount       = static_cast<std::uint8_t>(ShareComponents.size());

        // Compute how many data components we are going to have
        int EndDataIndex = static_cast<int>(m_ComponentInfos.size() - m_ShareComponentCount);

        // There should be at least an entity data component
        assert( EndDataIndex > 0);
        assert( Span.size()  <= m_ComponentInfos.size() );

        // Reserve virtual memory for our data components
        for( int i=0; i<EndDataIndex; ++i )
        {
            assert(m_ComponentInfos[i]->m_Size <= xecs::settings::virtual_page_size_v);
            auto nPages     = getPageFromIndex( *m_ComponentInfos[i], xecs::settings::max_entity_count_per_pool_v ) + 1;
            m_pComponent[i] = reinterpret_cast<std::byte*>(VirtualAlloc(nullptr, nPages * xecs::settings::virtual_page_size_v, MEM_RESERVE, PAGE_NOACCESS));
            assert(m_pComponent[i]);
        }

        // Store where the share component entity as a pointer since both are 64bits
        for( auto& E : ShareComponents )
        {
            m_pComponent[EndDataIndex++] = reinterpret_cast<std::byte*>(E.m_Value);
        }
    }

    //-------------------------------------------------------------------------------------

    void instance::Clear( void ) noexcept
    {
        while(m_Size)
        {
            Free( {m_Size-1}, true );
        }
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::getFreeSpace( void ) const noexcept
    {
        return  xecs::settings::max_entity_count_per_pool_v - m_Size;
    }

    //-------------------------------------------------------------------------------------

    index instance::Append( int Count ) noexcept
    {
        assert( (Count + m_Size) <= xecs::settings::max_entity_count_per_pool_v );

        for( int i = 0, end = static_cast<int>(m_ComponentInfos.size()- m_ShareComponentCount); i < end; ++i )
        {
            const auto&   MyInfo  = *m_ComponentInfos[i];
            auto          NexPage = getPageFromIndex(MyInfo, m_Size+Count);

            // Create pages when needed 
            if( auto Cur = getPageFromIndex(MyInfo, m_Size); m_Size == 0 || Cur != NexPage )
            {
                if( m_Size == 0 ) Cur--;
                auto pNewPagePtr = m_pComponent[i] + xecs::settings::virtual_page_size_v * (Cur+1);
                auto p           = reinterpret_cast<std::byte*>(VirtualAlloc(pNewPagePtr, (NexPage - Cur) * xecs::settings::virtual_page_size_v, MEM_COMMIT, PAGE_READWRITE));
                assert(p == pNewPagePtr);
            }

            // Construct if required
            if( MyInfo.m_pConstructFn ) 
            {
                auto p = &m_pComponent[i][m_Size * MyInfo.m_Size];
                for(int j=Count; j; --j )
                {
                    MyInfo.m_pConstructFn(p);
                    p += MyInfo.m_Size;
                }
            }
        }

        auto Index = m_Size;
        m_Size += Count;
        return { Index };
    }

    //-------------------------------------------------------------------------------------

    bool instance::Free( index Index, bool bCallDestructors ) noexcept
    {
        assert(Index.m_Value>=0);

        // Subtract one to the total count if we can.
        // If the last entry then happens to be a zombie then keep subtracting since these entries will get deleted later
        // This should make things faster and allow for the moved entries to be deleted properly 
        while(m_Size)
        {
            m_Size--;
            if(reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity) * m_Size]).isZombie() == false ) break;
        }

        // Check if we have any entry to move
        if( Index.m_Value >= m_Size )
        {
            // We are not moving anything just just call destructors if we have to
            if( bCallDestructors )
            {
                for (int i = 0; i < m_ComponentInfos.size(); ++i)
                {
                    const auto& MyInfo = *m_ComponentInfos[i];
                    auto        pData = m_pComponent[i];
                    if (MyInfo.m_pDestructFn) MyInfo.m_pDestructFn(&pData[Index.m_Value * MyInfo.m_Size]);
                }
            }
            return false;
        }
        else
        {
            for (int i = 0; i < m_ComponentInfos.size(); ++i)
            {
                const auto& MyInfo = *m_ComponentInfos[i];
                auto        pData  = m_pComponent[i];

                if ( MyInfo.m_pMoveFn )
                {
                    MyInfo.m_pMoveFn( &pData[Index.m_Value * MyInfo.m_Size], &pData[m_Size * MyInfo.m_Size] );
                }
                else
                {
                    if (bCallDestructors && MyInfo.m_pDestructFn) MyInfo.m_pDestructFn(&pData[Index.m_Value * MyInfo.m_Size]);
                    memcpy(&pData[Index.m_Value * MyInfo.m_Size], &pData[m_Size * MyInfo.m_Size], MyInfo.m_Size );
                }
            }
            return true;
        }
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::Size( void ) const noexcept
    {
        return m_CurrentCount;
    }

    //-------------------------------------------------------------------------------------

    constexpr
    int instance::findIndexComponentFromInfoInSequence
    ( const xecs::component::type::info& SearchInfo
    , int&                               Sequence
    ) const noexcept
    {
        const auto Backup = Sequence;
        for( const auto end = static_cast<const int>(m_ComponentInfos.size()); Sequence < end; ++Sequence)
        {
            if (&SearchInfo == m_ComponentInfos[Sequence]) return Sequence++;
            [[unlikely]] if ( xecs::component::type::details::CompareTypeInfos(&SearchInfo, m_ComponentInfos[Sequence])) break;
        }
        Sequence = Backup;
        return -1;
    }

    //-------------------------------------------------------------------------------------
    constexpr
    int instance::findIndexComponentFromInfo( const xecs::component::type::info& SearchInfo ) const noexcept
    {
        for( int i=0, end = static_cast<int>(m_ComponentInfos.size()); i<end; ++i )
        {
            if( &SearchInfo == m_ComponentInfos[i] ) return i;
            [[unlikely]] if(xecs::component::type::details::CompareTypeInfos(&SearchInfo, m_ComponentInfos[i] )) return -1;
        }
        return -1;
    }

    //-------------------------------------------------------------------------------------

    template
    < typename T_COMPONENT
    > requires
    ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
    )
    T_COMPONENT& instance::getComponent( const index EntityIndex ) const noexcept
    {
        if constexpr( std::is_same_v<xecs::component::entity, T_COMPONENT>)
        {
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[0][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
        else
        {
            const auto iComponent = findIndexComponentFromInfo( xecs::component::type::info_v<T_COMPONENT> );
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[iComponent][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
    }

    //-------------------------------------------------------------------------------------

    template
    < typename T_COMPONENT
    > requires
    ( std::is_same_v<T_COMPONENT, std::decay_t<T_COMPONENT>>
    )
    T_COMPONENT& instance::getComponentInSequence
    ( index     EntityIndex
    , int&      Sequence
    ) const noexcept
    {
        if constexpr (std::is_same_v<xecs::component::entity, T_COMPONENT>)
        {
            assert(Sequence==0);
            Sequence = 1;
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[0][EntityIndex.m_Value * sizeof(T_COMPONENT)]
            );
        }
        else
        {
            const auto iComponent = findIndexComponentFromGUIDInSequence(xecs::component::type::info_v<T_COMPONENT>.m_Guid, Sequence );
            return *reinterpret_cast<T_COMPONENT*>
            (
                &m_pComponent[iComponent][ EntityIndex.m_Value * sizeof(T_COMPONENT) ]
            );
        }
    }

    //-------------------------------------------------------------------------------------

    void instance::UpdateStructuralChanges( xecs::component::mgr& ComponentMgr ) noexcept
    {
        const auto OldSize = m_Size;

        //
        // Delete the regular entries
        //
        while( m_DeleteGlobalIndex != invalid_delete_global_index_v )
        {
            auto&       Entry                   = ComponentMgr.m_lEntities[m_DeleteGlobalIndex];
            auto&       PoolEntity              = reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity)*Entry.m_PoolIndex.m_Value]);
            const auto  NextDeleteGlobalIndex   = PoolEntity.m_Validation.m_Generation;
            assert(PoolEntity.m_Validation.m_bZombie);

            if( Free(Entry.m_PoolIndex, true) )
            {
                ComponentMgr.DeleteGlobalEntity(m_DeleteGlobalIndex, PoolEntity);
            }
            else
            {
                ComponentMgr.DeleteGlobalEntity(m_DeleteGlobalIndex);
            }

            m_DeleteGlobalIndex = NextDeleteGlobalIndex;
        }

        //
        // Delete Entities that were moved
        //
        while (m_DeleteMoveIndex != invalid_delete_global_index_v)
        {
            auto&       PoolEntity            = reinterpret_cast<xecs::component::entity&>(m_pComponent[0][sizeof(xecs::component::entity) * m_DeleteMoveIndex]);
            const auto  NextDeleteGlobalIndex = PoolEntity.m_Validation.m_Generation;
            
            if( Free({static_cast<int>(m_DeleteMoveIndex)}, false) )
            {
                ComponentMgr.MovedGlobalEntity({ static_cast<int>(m_DeleteMoveIndex) }, PoolEntity );
            }

            m_DeleteMoveIndex = NextDeleteGlobalIndex;
        }

        //
        // Free pages that we are not using any more
        //
        if( m_Size < OldSize )
        {
            for (int i = 0; i < m_ComponentInfos.size(); ++i)
            {
                const auto& MyInfo          = *m_ComponentInfos[i];
                const auto  LastEntryPage   = getPageFromIndex( MyInfo, OldSize );
                if( auto Cur = getPageFromIndex(MyInfo, m_Size); Cur != LastEntryPage || m_Size == 0 )
                {
                    if( m_Size == 0 ) Cur--;
                    auto pRaw   = &m_pComponent[i][xecs::settings::virtual_page_size_v * (Cur + 1) ];
                    auto nPages = LastEntryPage - Cur;
                    assert(nPages > 0);
                    auto b      = VirtualFree(pRaw, xecs::settings::virtual_page_size_v * nPages, MEM_DECOMMIT);
                    assert(b);
                }
            }
        }

        // Update the current count
        m_CurrentCount = m_Size;
    }

    //-------------------------------------------------------------------------------------
    inline
    void instance::Delete( index Index ) noexcept
    {
        auto& Entity = getComponent<xecs::component::entity>(Index);
        assert(Entity.m_Validation.m_bZombie);
        Entity.m_Validation.m_Generation = m_DeleteGlobalIndex;
        m_DeleteGlobalIndex = Entity.m_GlobalIndex;
    }

    //--------------------------------------------------------------------------------------------
    constexpr
    bool instance::isLastEntry( index Index ) const noexcept
    {
        return Index.m_Value == m_Size - 1;
    }

    //--------------------------------------------------------------------------------------------
    inline
    index instance::MoveInFromPool( index IndexToMove, pool::instance& FromPool ) noexcept
    {
        const auto iNewIndex = Append(1);

        int         iPoolFrom       = 0;
        int         iPoolTo         = 0;
        const int   PoolFromCount   = static_cast<int>(FromPool.m_ComponentInfos.size()  - FromPool.m_ShareComponentCount);
        const int   PoolToCount     = static_cast<int>(m_ComponentInfos.size()           - m_ShareComponentCount);
        while( true )
        {
            if( FromPool.m_ComponentInfos[iPoolFrom] == m_ComponentInfos[iPoolTo] )
            {
                auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
                if( Info.m_pMoveFn )
                {
                    Info.m_pMoveFn
                    (
                        &m_pComponent[iPoolTo][ Info.m_Size * iNewIndex.m_Value ]
                    ,   &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * IndexToMove.m_Value ]
                    );
                }
                else
                {
                    std::memcpy
                    ( 
                        &m_pComponent[iPoolTo][ Info.m_Size * iNewIndex.m_Value ]
                    ,   &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * IndexToMove.m_Value ]
                    ,   Info.m_Size
                    );
                }
                iPoolFrom++;
                iPoolTo++;
                if ( iPoolFrom >= PoolFromCount || iPoolTo >= PoolToCount )
                    break;
            }
            else if(FromPool.m_ComponentInfos[iPoolFrom]->m_Guid.m_Value < m_ComponentInfos[iPoolTo]->m_Guid.m_Value )
            {
                auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
                if( Info.m_pDestructFn ) Info.m_pDestructFn( &FromPool.m_pComponent[iPoolFrom][ Info.m_Size * IndexToMove.m_Value ] );

                iPoolFrom++;
                if( iPoolFrom >= PoolFromCount )
                    break;
            }
            else
            {
                // This is already constructed so there is nothing for me to do
                iPoolTo++;
                if( iPoolTo >= PoolToCount )
                    break;
            }
        }

        //
        // Destruct any pending component
        //
        while (iPoolFrom < PoolFromCount)
        {
            auto& Info = *FromPool.m_ComponentInfos[iPoolFrom];
            if (Info.m_pDestructFn) Info.m_pDestructFn(&FromPool.m_pComponent[iPoolFrom][ Info.m_Size * IndexToMove.m_Value ]);
            iPoolFrom++;
        }

        //
        // Put the deleted entity into the move deleted linklist
        //
        {
            auto& Entity = FromPool.getComponent<xecs::component::entity>(IndexToMove);
            Entity.m_Validation.m_bZombie    = true;
            Entity.m_Validation.m_Generation = FromPool.m_DeleteMoveIndex;
            FromPool.m_DeleteMoveIndex       = IndexToMove.m_Value;
        }

        return iNewIndex;
    }

}