struct space_ship_logic : xecs::system::instance
{
    constexpr static auto typedef_v = xecs::system::type::update
    {
        .m_pName = "space_ship_logic"
    };

    xecs::query::instance       m_QueryThinkingShipsOnly    {};
    xecs::query::instance       m_QueryAnyShips             {};
    xecs::prefab::guid          m_BulletPrefab              {};

    void OnGameStart( void ) noexcept
    {
        m_QueryThinkingShipsOnly.m_Must.AddFromComponents<position>();
        m_QueryThinkingShipsOnly.m_NoneOf.AddFromComponents<bullet, timer>();

        m_QueryAnyShips.m_Must.AddFromComponents<position>();
        m_QueryAnyShips.m_NoneOf.AddFromComponents<bullet>();

        // Create the prefab for the bullets...
        m_BulletPrefab = CreatePrefab<position, velocity, bullet, timer, grid_cell>([&](timer& Timer) noexcept
        {
            Timer.m_Value = 10;
        });
    }

    using query = std::tuple
    <
        xecs::query::must<xecs::component::share_as_data_exclusive_tag>
    >;

    __inline
    void operator()( const grid_cell& GridCell, const xecs::component::share_filter& ShareFilter ) noexcept
    {
        Foreach(ShareFilter, m_QueryThinkingShipsOnly, [&]( entity& Entity, const position& Position ) constexpr noexcept
        {
            grid::Search( *this, ShareFilter, GridCell.m_X, GridCell.m_Y, m_QueryAnyShips, [&]( const entity& E, const position& Pos ) constexpr noexcept
            {
                // Don't shoot myself
                if ( Entity == E ) return false;

                auto        Direction        = Pos.m_Value - Position.m_Value;
                const auto  DistanceSquare   = Direction.getLengthSquared();

                // Shoot a bullet if close enough
                if(constexpr auto min_distance_v = 60; DistanceSquare < min_distance_v*min_distance_v )
                {
                    auto NewEntity = AddOrRemoveComponents<std::tuple<timer>>( Entity, [&]( timer& Timer )
                    {
                        Timer.m_Value = 8;
                    });

                    // After moving the entity all access to its components via the function existing parameters is consider a bug
                    // Since the entity has moved to a different archetype
                    assert( Entity.isZombie() );

                    // Hopefully there is not system that intersects me and kills me
                    assert( !NewEntity.isZombie() );

                    CreatePrefabInstance( 1, m_BulletPrefab, [&]( position& Pos, velocity& Vel, bullet& Bullet, grid_cell& Cell) noexcept
                    {
                        Direction  /= std::sqrt(DistanceSquare);
                        Vel.m_Value = Direction * 2.0f;
                        Pos.m_Value = Position.m_Value + Vel.m_Value;

                        Bullet.m_ShipOwner = NewEntity;

                        Cell = grid::ComputeGridCellFromWorldPosition(Pos.m_Value);
                    });

                    return true;
                }

                return false;
            });
        });
    }
};
