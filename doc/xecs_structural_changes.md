<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" /><br>
# [xECS](xecs.md) / Structural Changes

Structural changes in an ECS means that an entity was either created, deleted, or a combination of both (such in the case of adding or removing components, which causes an entity to be created in a new archetype, its data copied and deleted from the old archetype, check [Archetype](xecs_archetype.md) for more information).

These structural changes is probably one of the most dangerous things than an ECS does, and can cause many bugs for the user as well as performance issues. There are two ways to solve this problem:

1. The Unity 3D way. How they solved it is that all commands that causes structural changes are queue and executed after the system ends up running. This method has the side effect of using more memory and been less performant, as well as loosing some functionality, but the positive side is that is very easy to program and very safe. 
2. The xECS way. It executed everything right away, while trying to maintain reasonable expectations from the user. This makes the code a bit more complex for the user because certain system need to check if an entity is already dead, but this also allows the user to have all the functionality unlike Unity, and in terms of memory and performance is allot faster. The negative side is that it has some side effects which we cover in this section.

| TODO::page_with_curl: | xECS needs to add support for an entity that has changed the [share-component](xecs_component_types_share.md) data and also deleted itself. This is currently not working. | 
|:---:|---|

## Updating structural changes

This happens at the very end of a system execution and before the next system runs. This is an action that all system make and it is used basically to clean up and normalize the pools as well as any other data structures that xECS needs. This is invisible to the user but for advance user is a good to know thing. 

## Creation of new entities

The creation of new entities is hidden by xECS until the system processing is done. The way xECS does this is by having two counters in the **xecs::pool::instance**. One counter tells how many entities that executing system must assume the pool has (**xecs::pool::instance::m_CurrentCount**), while the second counter is how many entities the pool really has (**xecs::pool::instance::m_Size**).

This means that creation in xECS is really fast since the memory is the final memory yet it looks and feels basically like Unity.

## Deletion of entities

This is the most complex operation. xECS as part of the xecs::component::entity has a member function called ***isZombie()*** this is something that Unity3D, does not have. This allows xECS to mark an entity as "Deleted", and this entity is added into a link-list of entities for the given pool that are also meant to be deleted. However nothing else of the particular entity is touched.

When it gets time to resolve structural changes then xECS goes throw the link-list officially deleting entities as well as patching holes created by deleting those entities. Pools must always keep their memory contiguous. 

This means that xECS can delete entities without queuing them like Unity3D, as well as letting the user know which entities have been deleted, which allows O^2 collision/destruction checks which Unity3D does not support.

## Moving entities 

Moving entities from one [Archetype](xecs_archetype.md) to another is cause by adding or deleting components in an Entity. Like mention previously moving entities is a combination of Creation, Possibly copying data, and Deletion of the old entity. Which if we study the previous point you can see that in the moment that we process one entity and move it from the [Archetype](xecs_archetype.md), it will become invisible until the system gets to update its structural changes. This automatically solves the problem of executing the same entity multiple times by a system.

Unlike Unity3D this is not queue and the execution happens right away, which means that all additional data that new components may need gets resolved right away.

| :warning: | <span style="color:yellow">There is one negative thing that it could happen using the xECS way. Is that even though a moved entity is invisible a system may want to do another loop and revisit this entity, however it won't be able to find it since it was created and the update structural changes has not been executed. This is a very edge case, and for now xECS will ignore it.</span> |
|:---:|---|

## Moving cause by changing the value of share-components

Each entity is store inside a ***xecs::pool::family***. A [family](xecs_component_types_share.md) is a set of specific values of an entity [share-components](xecs_component_types_share.md). Which means that if a one of the share-component values of an entity changes the entity must be moved to the right [family](xecs_component_types_share.md). Moving the entity work like we explain previously, however besides moving the entity we also have a new family which also must be added to the [Archetype](xecs_archetype.md). We don't want to change the number of families for an archetype right away to prevent unexpected consequences. (Such a system doing a double iteration throw the same archetypes, even though there should be not visible entities for those new families, they really add nothing but overhead.) Father more [share-components](xecs_component_types_share.md) have something call filters. Updating the share filter is in fact the main reason to postpone the reveal of the new family. Since someone could be going throw all the families of a particular [Archetype](xecs_archetype.md). Changing that vector right away could cause the vector to reallocate and mess up the iterator.

| :x::skull:| <span style="color:red"> xECS does not support a System that updates a [share-component](xecs_component_types_share.md) data and at the same time also adds new components as well. </span>| 
|:---:|---|

---