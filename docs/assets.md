# Assets

## General 

Currently we have the following asset types: 
* Scenes
* Prototypes: sub-scenes that can be instantiated within a regular scene or other prototypes
* Meshes

All assets have a randomly generated 64 bit globally unique identifier (GUID) assigned to each constituent part.
For example, for a scene or prototype, a GUID is generated for each node in that scene or prototype.
For prototypes a GUID is also generated for the prototype itself.

For assets that were created in the editor, GUID are stored in the assets themselves.
For imported assets, GUIDs are stored in meta files.

Assets are stored in the `assets` directory in the project directory.
Compiled data is stored in the `content`.

## Scenes and prototypes

Scenes are represented by hierarchy of nodes.
Each node has a GUID, a transform (translation, orientation (as a unit quaternion) and scale), and a parent (except root nodes).
Nodes can also optionally contain data. Data is stored as a tagged union.
This means that each node corresponds to a single entity with optionally a single component attached to it.

Scenes are stored in `assets/scenes`.

Example of a scene asset:
```json
{
  "version": "0.1.0",
  "environment": {
    "luminance": [0, 0, 10000]
  },
  "nodes": [
    {
      "name": "First",
      "guid": "44AE80D152FDB788",

      "position": [0.0, 10.0, 20.0],

      "camera": {
        "projection": "pespective"
      }
    },
    {
      "name": "Second",
      "guid": "E6E759AA4EB5454C",
      "parent": "44AE80D152FDB788",

      "position": [0.0, 20.0, 20.0],
      "rotation": [0.0, 0.0, 1.0, 1.0],
      "scale": [0.1, 0.5, 1.5],

      "mesh": {
        "asset": "F7CA46B3C9492F53",
        "casts_shadows": true
      }
    },
    {
      "name": "Third",
      "guid": "62913C692416E210",

      "position": [0.0, 20.0, 20.0],
      "rotation": [0.1, 0.5, 0.2, 1.0],
      "scale": [0.1, 0.5, 1.5],

      "point_light": {
        "intencity": 800,
        "color": [ 1.0, 1.0, 1.0 ]
      }
    }
  ]
}
```

Prototypes are scenes on which you can't set some scene specific global properties.
Prototypes can be referenced by scenes and other prototypes and are copied into that scene or prototype when it's instantiated.

Prototypes are stored in `assets/prototypes`.

Example of a prototype:
```json
{
  "version": "0.1.0",
  "guid": "14C756EE2D9234BE",
  "nodes": [
    {
      "name": "Prototype with mesh",
      "guid": "EA6D009275D9950E",
      "mesh": {
        "asset": "F7CA46B3C9492F53",
        "casts_shadows": true
      }
    }
  ]
}
```

Instantiated in a scene:
```json
{
  "version": "0.1.0",
  "nodes": [
    {
      "name": "Scene with prototype",
      "guid": "6ED3EE26E93FE038",
      "prototype": "14C756EE2D9234BE"
    }
  ]
}
```

### Prototype inheritance
Prototypes support inheritance.
Child prototypes can override the value of their parent's fields or add new fields, but not remove fields.
Child prototypes also can't change the data type of a node (this restriction might get removed in the future).

Prototype inheritance example:
```json
{
  "version": "0.1.0",
  "guid": "214F5F7A426F6F46",
  "inherits": "14C756EE2D9234BE",
  "nodes": [
    {
      "name": "Child prototype with mesh",
      "position": [0.0, 0.0, -1.0],
      "mesh": {
        "casts_shadows": false
      }
    }
  ]
}
```

## Meshes

We use glTF 2.0 as the source format for geometry.
Other formats are also supported, but they are converted to glTF using Assimp when they are imported.

glTF files define scenes with lights, materials, cameras, etc.
We don't use most of that stuff and remove it on import.

Meshes are created from each glTF primitive. A GUID is generated for each mesh.

Prototypes are generated from each glTF scene in the file.

glTF files are stored in `assets/glTF`. Meshes are stored in `content/meshes`. Generated prototypes are stored in `assets/glTF`. 

## Asset compilation

Asset compilation must be performed in the following cases:
* A generated asset is missing, because a new asset was imported or because it was deleted 
* An asset is updated (not supported at the moment)
* A meta file is updated

A list of assets that need compilation is maintained.
Compilation is performed when the user requests it.

### Unused asset cleanup

If a meta file is missing for an asset, the asset file is deleted.
Similarly, if a meta file's asset is missing, it is deleted. 
Any compiled content whose GUID is unknown is also deleted.

Cleanup is performed as part of the next compilation process.
