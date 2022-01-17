<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" />

# [xECS](xECS.md) / [Scene](editor.md) / Serialization

Scenes knows which local-entities they need to save because they have a list of those entities. This is the same list that is used to remap entity references. These are contexts cases when user want to save Scenes:

1. Editor
2. Casual app

Loading scenes:

1. Editor
2. Casual app
3. Runtime

These are the different kinds of entities that the file needs to be able to serialize:

| Type of Entity | Prefab Instance<br>(Entity or Scene) | Comments |
|:--------------:|:---------------:|----------|
| Share Entity   |        N (only) | Share entities are created by the system and their job is to hold the share-components |
| Local Entity   |        N        | Regular entities created using the default API which can't have references outside their scene.<br> This entities will need their references to be remapped. |
| Local Entity   |        Y        | Prefabs Instances  which can't have references outside their scene.<br> This entities will need their references to be remapped. |
| Global Entity  |        N        | Regular entities that any other entities can refer to them, and their ids are created in ranges |
| Global Entity  |        Y        | Prefabs Instances, that any other entity can have a reference to them, and their id are created in ranges |
| Prefab Entity  |        N        | Prefabs are similar to Global Entities however their range is organize a bit different. |
| Prefab Entity  |        Y        | These are known as Variants. are similar to Global Entities however their range is organize a bit different. |

## Serializer Modes

Since only the entities of a particular scene needs to be saved and those are mix with all other entities we must filter out entities that don't belong 
to achieve this the serializer may need to do multiple passes to collect the require information.

There different modes to utilize the Serializer:

1. Serialize individual Scenes
2. Save the entire Game-State not matter the Scene Loaded or Prefabs
3. Save a Prefab
4. Save a Prefab Variant 

For more in depth about [Serialization and Streaming](xecs_scene_serialization_streaming.md)

## Challenges 

The serialization of entities for the use of the editor requires us to make certain compromises. For instance future proving is more important than performance. Since things will be changing and we don't want to loose our work. Some of the challenges are:

* Must survive the mutations/existence of properties
* Must survive the mutations/existence of components (data, share, tags...)
* Must survive the mutations/existence of prefabs
* Deal with Global Entities
* Deal with remapping of entities
* Have nice warning/error reports

## Ranges

When saving Scenes it is important to include the range allocation. Note that the Range File could be also consider part of the serialization.

## Sections of the file

* [Ranges]()
* [General]()
* [Entities]()
