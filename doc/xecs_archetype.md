<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Archetype

Archetypes defines a unique set of components for an entity. All entities with the same set of components will end up in the same archetype, even if they belong to a different scene. This allow for fast queries, good memory management, and fast entity iteration by systems. 

## GUID

Archetypes have a Global Unique Identifier which is computed using the following formula:

~~~c++
Archetype::GUID = Sum_All( Component::GUID.m_Value )
~~~

Given such a simple formula to compute the Archetype GUID it is possible to create collisions, however the expectation of the actual collision is perceived to be rare that does not deserve to make it any more complex than this.

This GUID is used for fast look ups via a "**std::unorded_map**" So when the user request to get or create an archetype it can quickly determine if it has it or not. This map is located in the **xecs::archetype::mgr::m_ArchetypeMap**.

## Archetype Owner

The memory of the archetypes are store in the **xecs::archetype::mgr::m_lArchetype** The type of this member variable: **std::vector< std::unique_ptr< xecs::archetype::instance > >**. This allows to iterate if we can throw all the archetypes easily. Also when the archetype::mgr dies it automatically destroys all the Archetypes. 

## Searches

In ECS it is very important to do searches for the right Archetypes, to speed up this we have **xecs::archetype::mgr::m_lArchetypeBits** This is member variable that is a **std::vector** which which contains all the bits of a given archetype. This data is extracted so that we don't pay the price of a cache miss do to the archetype pointer indirection. However the price of this is a bit more memory as well as we been in sync with **xecs::archetype::mgr::m_lArchetype**.

## Structural Changes

Structural changes are schedule very carefully, for xECS it tries to commit changes as soon as it can unlike Unity which buffers the creation, deletion, and adding and removing components. For the perfective on an archetype the only thing that postpone execution is when adding a new [xecs::pool::family](xecs_component_types_share.md). For more information about [Structural Changes](xecs_structural_changes.md)

## Scenes
