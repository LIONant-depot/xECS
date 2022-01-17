<img src="https://i.imgur.com/TyjrCTS.jpg" align="right" width="220px" />
# [xECS](xECS.md) / [Scene](editor.md) / [Serialization]() / General information

## FileInfo

In order to know which version of the serialization we are using we will save a Label, Version, Date and Time.

***Example:***
~~~cpp
[ FileInfo ]
{ Type:s           Version:ddd  AppCompilationDate:s   CreationDate:s }
//---------------  -----------  --------------------  ------------------
  "xECS - Scene"     1  0  3     "21:32:4 2/4/2022"   "21:32:4 2/4/2022"
~~~

| Column Name        | Description |
|:------------------:|-------------|
| Type               | Type of file that this is | 
| Version            | Major: Breaking changes of the file format <br> Minor: changes does not break the API but it has a significant change none the less <br> Patches: Bug fixes | 
| AppCompilationDate | The time when the application was compiled |
| CreationDate       | The time when the file was created/exported |

## Map properties types to xcore::textfile types
xECS uses xcore::textfile to serialize its entities. Since properties are a typical way to serialize entities xECS will always create a user define block to map between the property types with the xcore::textfile types. Here is what you will see in to the of the file. Note that this may not look identical to you case as every application may have added a different set of properties.

***Example:***
~~~cpp
// New Types
< d:d b:c f:f s:s v2:ff entity:G C:C >
~~~

***Questions:***

>*What would happen if a game that is trying to load the file has less propertie types?*
xcore::textfile and xcore::properties are able to handle this case just fine. However the entity's properties that had those types will be ignore.

>*What would happens if we change the name of one of the property types?*
If the user still provides a mapping it should still work, other wise those properties will be ignore.

:warning: **TODO: We need to make this table as part of the user-settings**

## SceneInfo

The block contain information relevant to the entire scene. Remember that an archetype is a unique set of components, not matter of what type they are. Also note that prefabs/variants are saved separately using a different function. So xECS serialize will ignore any archetype with the xecs::prefab::tag in it. So the scene sequence is: 

1. Local Archetypes
2. Local Archetypes with prefabs
3. Global Archetypes
4. Global Archetypes with prefabs

Since all entities are mix together we need to be able to identify which entities belong to which scene. 

***Example:***
~~~cpp
[ SceneInfo ]
{  Name:s         Guid:G            nSharedArchetypes:d  nLocalArchetypes:d  nGlobalArchetypes:d }
//--------------  ----------------  -------------------  ------------------  --------------------
   "Scene Name"    #423FE34234123             5                   4                   2 
~~~

| Column Name       | Description |
|:-----------------:|-------------|
| Name              | Name of the scene |
| Guid              | Guid of this scene |
| nSharedArchetypes | Number of share Archetypes. These are archetypes that have share-entities only. |
| nLocalArchetypes  | Number of local Archetypes (Prefabs or not). These are archetypes that have local-entities only. | 
| nGlobalArchetypes | Number of global Archetypes (Prefabs or not). These are archetypes that have global-entities only. | 


---