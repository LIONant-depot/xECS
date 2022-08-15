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

Structural changes are schedule very carefully, for xECS it tries to commit changes as soon as it can unlike Unity which buffers the creation, deletion, and adding and removing components. For the perfective on an archetype the only thing that postpone execution is when adding a new **xecs::pool::family**. There are two reasons why new families are added:

1. An entity changed the value of one of its share components, and there was not family with those specific values.
2. A system adds a share component to an entity and there was not family with that specific value.

This new family is hidden until xECS system finish running, this is because we want to prevent the iterator from iterating inside the new family. Because if it does, then the system would run the same entity twice.

It is true that this solves the case for moved entities, but what about:

a) entities that don't require new families
b) system that update the share component and add new components as well. (this one could be specially dangerous.)


Updating the share filter is in fact the main reason to postpone the reveal of the new family. Since someone could be going throw all the families of a particular Archetype. Changing that vector right away could cause the vector to reallocate and mess up the iterator. Indirectly this solves the problem of iterating throw the same entity twice only in the case of new family.

While is true that new added entities are hidden until the structures changes are executed this could have also a side effect of loosing an entity because of components add/delete in a double loop.


---

For Archetypes structural changes only happens when new families are added to the archetype. 


Structural changes for an archetype only means that the archetype has been created. It is important for the system to know this because the archetype should remain hidden until the system finish running. Because from the point of view an a system nothing should really change except entities becoming zombies. (WHAT ABOUT ENTITIES THAT COMPONENTS ARE ADDED/DELETED????) 

---

## Families


Structural changes happens when entities are created or destroyed. Some functions such adding adding components to an entity do both (deletes one into from one archetype and creates another entity in another archetype). Please note that unlike Unity, xECS does not queue the operation, the action happens immediately, what happens after wards is the update of the pools. A system that may iterate throw a list of entities twice can check if an entity is deleted by checking if the entity is a zombie or not. If the system is not deleting entities it does not need to check for anything.

## Pools

## Scenes