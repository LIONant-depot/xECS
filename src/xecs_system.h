namespace xecs::system
{
    namespace type
    {
        using guid = xcore::guid::unit<64, struct system_tag>;

        enum class id : std::uint8_t
        {
            UPDATE
        ,   NOTIFY_CREATE
        ,   NOTIFY_DESTROY
        ,   NOTIFY_MODIFIED
        ,   NOTIFY_MOVE_IN
        ,   NOTIFY_MOVE_OUT
        ,   NOTIFY_COMPONENT_CHANGE
        ,   NOTIFY_COMPONENT_ADDED
        ,   NOTIFY_COMPONENT_REMOVE
        ,   POOL_FAMILY_CREATE
        ,   POOL_FAMILY_DESTROY
        ,   GLOBAL_EVENT
        ,   SYSTEM_EVENT
        };

        struct update
        {
            static constexpr auto       id_v                = id::UPDATE;
            static constexpr auto       is_notifier_v       = false;
            const char*                 m_pName             = "Unnamed Update System";
            guid                        m_Guid              {};
        };

        struct notify_create
        {
            static constexpr auto       id_v                = id::NOTIFY_CREATE;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Create Entity System";
            guid                        m_Guid              {};
        };

        struct notify_destroy
        {
            static constexpr auto       id_v                = id::NOTIFY_DESTROY;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Destroy Entity System";
            guid                        m_Guid              {};
        };

        struct notify_moved_in
        {
            static constexpr auto       id_v                = id::NOTIFY_MOVE_IN;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Move In Entity System";
            guid                        m_Guid              {};
        };

        struct notify_moved_out
        {
            static constexpr auto       id_v                = id::NOTIFY_MOVE_OUT;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Notified Move Out System";
            guid                        m_Guid              {};
        };

        struct notify_component_change
        {
            static constexpr auto        id_v               = id::NOTIFY_COMPONENT_CHANGE;
            static constexpr auto        is_notifier_v      = true;
            const char*                  m_pName            = "Unnamed Component Change System";
            guid                         m_Guid             {};
      const xecs::component::type::info* m_pComponentInfo   {};
        };

        struct pool_family_create
        {
            static constexpr auto       id_v                = id::POOL_FAMILY_CREATE;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Pool Family Create System";
            guid                        m_Guid              {};
        };

        struct pool_family_destroy
        {
            static constexpr auto       id_v                = id::POOL_FAMILY_DESTROY;
            static constexpr auto       is_notifier_v       = true;
            const char*                 m_pName             = "Unnamed Pool Family Create System";
            guid                        m_Guid              {};
        };

        template< typename T_EVENT >
        requires( std::derived_from< T_EVENT, xecs::event::overrides> )
        struct global_event
        {
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::GLOBAL_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed Global Event System Delegate";
            guid                         m_Guid             {};
        };

        template< typename T_SYSTEM, typename T_EVENT >
        requires( std::derived_from< T_SYSTEM, xecs::system::instance>
                  && ( xcore::types::is_specialized_v<xecs::event::instance, T_EVENT>
                       || std::derived_from< T_EVENT, xecs::event::overrides>) )
        struct system_event
        {
            using                        system_t           = T_SYSTEM;
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::SYSTEM_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed System Event Delegate";
            guid                         m_Guid             {};
        };

        template< typename T_SYSTEM, typename T_EVENT >
        requires( std::derived_from< T_SYSTEM, xecs::system::instance>
                  && ( xcore::types::is_specialized_v<xecs::event::instance, T_EVENT>
                       || std::derived_from< T_EVENT, xecs::event::overrides>) )
        struct child_update
        {
            using                        system_t           = T_SYSTEM;
            using                        event_t            = T_EVENT;
            static constexpr auto        id_v               = id::SYSTEM_EVENT;
            static constexpr auto        is_notifier_v      = false;
            const char*                  m_pName            = "Unnamed Child Update Delegate";
            guid                         m_Guid             {};
        };

        struct info
        {
            using notifier_registration = void( xecs::archetype::instance&, xecs::system::instance&);

            type::guid                              m_Guid;
            mutable xecs::query::instance           m_Query;
            notifier_registration*                  m_NotifierRegistration;
            const char*                             m_pName;
            id                                      m_ID;
        };

        namespace details
        {
            template< typename T >
            consteval type::info CreateInfo(void) noexcept;

            template< typename T >
            static constexpr auto info_v = CreateInfo<T>();
        }

        template< typename T_SYSTEM >
        constexpr static auto& info_v = details::info_v<T_SYSTEM>;
    }

    //-----------------------------------------------------------------
    // ECS USER DEFINITIONS
    //-----------------------------------------------------------------
    struct overrides
    {
        using                   entity      = xecs::component::entity;      // Shortcut for entity
        using                   query       = std::tuple<>;                 // Override this to specify the query for the system
        using                   events      = std::tuple<>;                 // Override this to create events which other systems can use
        constexpr static auto   typedef_v   = type::update{};               // Override this to specify which type of system this is

        void    OnCreate                ( void )                noexcept {} // All Systems:         When the system is created
        void    OnGameStart             ( void )                noexcept {} // All Systems:         When the game starts or when it becomes unpaused
        void    OnFrameStart            ( void )                noexcept {} // All Systems:         At the begging of a frame
        void    OnPreUpdate             ( void )                noexcept {} // Update Systems:      Is call just before the OnUpdate gets call
        void    OnUpdate                ( void )                noexcept {} // Update Systems:      If you want full control of the query
        void    OnPostUpdate            ( void )                noexcept {} // Update Systems:      Is call just after the OnUpdate gets call and before structural changes happen
        void    OnPostStructuralChanges ( void )                noexcept {} // Update Systems:      After the Structural changes has taken place (applies only to )
        void    OnFrameEnd              ( void )                noexcept {} // All Systems:         Beginning of every frame
        void    OnGameEnd               ( void )                noexcept {} // All Systems:         When the game is done
        void    OnDestroy               ( void )                noexcept {} // All Systems:         Before destroying the system
        void    OnGamePause             ( void )                noexcept {} // All Systems:         When the game is paused 
        void    OnEvent                 ( ...  )                noexcept {} // Event System:        User overrides to receive the event message
        void    OnNotify                ( entity& Entity )      noexcept {} // Notify Systems:      Advance control
        void    OnPoolFamily            ( archetype::instance&              // Pool Family System:  Gets notified when a pool is created or destroy
                                        , pool::family&       ) noexcept {}
    };

    struct instance : overrides
    {
                    instance            ( const instance&
                                        ) noexcept = delete;

        constexpr   instance            ( xecs::game_mgr::instance& GameMgr
                                        ) noexcept;

        template
        < typename   T_EVENT
        , typename   T_CLASS
        , typename...T_ARGS
        > requires
        ( std::derived_from<T_CLASS, xecs::system::instance>
            && (false == std::is_same_v<typename T_CLASS::events, xecs::system::overrides::events>)
            && !!(xcore::types::tuple_t2i_v<T_EVENT, typename T_CLASS::events > +1)
        ) __inline
        static void SendEventFrom(T_CLASS* pThis, T_ARGS&&... Args) noexcept;

        xecs::game_mgr::instance&   m_GameMgr;
    };

    //-----------------------------------------------------------------
    // ECS SYSTEM DEFINITIONS
    //-----------------------------------------------------------------
    struct mgr final
    {
        struct events
        {
            xecs::event::instance<>     m_OnGameStart;
            xecs::event::instance<>     m_OnGameEnd;
            xecs::event::instance<>     m_OnUpdate;
            xecs::event::instance<>     m_OnFrameStart;
            xecs::event::instance<>     m_OnFrameEnd;
        };

                                mgr                 ( const mgr&
                                                    ) noexcept = delete;
                                mgr                 ( void
                                                    ) noexcept = default;
        template
        < typename T_SYSTEM
        > requires
        ( std::derived_from< T_SYSTEM, xecs::system::instance>
        )
        T_SYSTEM&               RegisterSystem      ( xecs::game_mgr::instance& GameMgr 
                                                    ) noexcept;
        inline 
        void                    Run                 ( void 
                                                    ) noexcept;
        template< typename T_SYSTEM >
        T_SYSTEM*               find                ( void
                                                    ) noexcept;
        inline
        void                    OnNewArchetype      ( xecs::archetype::instance& Archetype
                                                    ) noexcept;

        using system_list      = std::vector< std::pair<const xecs::system::type::info*, std::unique_ptr<xecs::system::instance>> >;

        std::unordered_map<type::guid, xecs::system::instance*> m_SystemMaps;
        system_list                                             m_UpdaterSystems;
        system_list                                             m_NotifierSystems;
        events                                                  m_Events;
    };
}