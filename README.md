# Box3DUnreal4-Networked

Box3DUnreal4-Networked is an Unreal Engine 4 integration for [Box3D](https://github.com/erincatto/box3d), Erin Catto's lightweight 3D rigid-body physics engine. It provides a fixed-step Box3D world, Unreal actor/component bindings, static collision extraction, instanced rigid bodies, spatial queries, debug drawing, and an authoritative multiplayer path.

The plugin is useful when you want a small, deterministic physics simulation alongside Unreal. Box3D bodies are separate from PhysX bodies: an actor controlled by Box3D is not simulated by PhysX.

> **Status:** The integration is actively evolving. Test the settings and network behavior against your project's content and target hardware.

## Features

- Dynamic, kinematic, and static Box3D bodies.
- Box, sphere, capsule, automatic bounds, and convex simple-collision shapes.
- Static simple collision, complex tri-mesh collision, landscapes, tagged bulk geometry, and pre-baked collision assets.
- A normal one-body-per-actor `UBox3DBodyComponent` path.
- A fast `ABox3DInstancedBodyActor` path that stores many bodies in one actor and renders them with a HISM.
- Server-authoritative replication, client interpolation, input requests, bounded rewind history, and state reconciliation.
- Optional local-only instanced simulation for single-player projects.
- Fixed timestep, solver substeps, configurable gravity, solver worker count, sleeping, damping, friction, restitution, and collision filtering.
- Blueprint-callable impulses and spatial queries: closest/multi raycasts, sphere and box casts, AABB overlap, sphere overlap, and oriented-box overlap.
- Runtime enable/disable through `box3d.Enabled`, with optional launch-time disable.
- Debug visualization and test/diagnostic console commands.
- Editor commandlet for cooking static collision into `UBox3DCollisionData` assets for packaged builds.

## Requirements and installation

1. Clone the repository with its submodule:

   ```bash
   git clone --recurse-submodules https://github.com/RubidiumLemur/Box3DUnreal4-Networked.git
   ```

2. Put the plugin in either:

   ```text
   <YourProject>/Plugins/Box3DUnreal
   ```

   or:

   ```text
   <UnrealEngine>/Engine/Plugins/Box3DUnreal
   ```

3. Enable the plugin, regenerate project files, and build the project. The included `Content` folder contains example assets and maps.

The integration uses Unreal centimeters in its public settings and converts to Box3D's meter-based simulation internally. Keep physically simulated objects at sensible real-world scale.

## Quick start: normal component path

Use this path when each simulated object needs its own actor, mesh, gameplay logic, or per-object settings.

1. Add a Static Mesh Component to an actor and set its mobility to **Movable** for dynamic or kinematic bodies.
2. Add **Box3D Body Component** to the actor.
3. Set `Body Type` to `Dynamic` for Box3D-driven movement, `Kinematic` for gameplay-driven movement, or `Static` for level collision.
4. Choose `Shape`:
   - `Auto`: derives a box from the root primitive bounds.
   - `Box`: uses `Box Half Extent`.
   - `Sphere`: uses `Radius`.
   - `Capsule`: uses `Radius` and `Half Height`.
   - `Convex`: imports the mesh's simple convex/box collision; it falls back to a box when no usable simple collision exists.
5. For a dynamic body, apply impulses with `AddImpulse`. The server/standalone authority owns the final state.

For networked level actors, enable **Replicates** on the actor in the editor. The component does not simulate a pure client; clients receive authoritative transforms and smooth them through Unreal replication/reconciliation.

### Component settings

| Category | Setting | Purpose |
| --- | --- | --- |
| Box3D | `Body Type` | `Static`, `Kinematic`, or `Dynamic`. |
| Box3D | `Shape` | Automatic box, box, sphere, capsule, or simple-collision convex hull(s). |
| Shape | `Box Half Extent` | Box half-extents in cm; used by `Box`. |
| Shape | `Radius` | Radius in cm; used by `Sphere` and `Capsule`. |
| Shape | `Half Height` | Distance from capsule center to either hemisphere center, in cm; used by `Capsule`. |
| Static | `Static Source` | `Auto`, `Simple Collision`, or `Complex Collision` for static bodies. |
| Static | `Invert Mesh Winding` | Reverses one-sided triangle winding when a mesh falls through or faces the wrong way. |
| Material | `Density` | Density in kg/m³; water is approximately 1000. |
| Material | `Friction` | Surface friction. |
| Material | `Restitution` | Bounciness. |
| Material | `Rolling Resistance` | Rolling resistance for spheres and capsules. |
| Damping | `Linear Damping` | Reduces linear velocity over time. |
| Damping | `Angular Damping` | Reduces angular velocity over time. |
| Sleeping | `Enable Sleep` | Allows dynamic bodies to sleep when sufficiently still. |
| Sleeping | `Sleep Threshold` | Linear speed threshold in cm/s for sleeping. |
| Collision | `Collision Category` | Bit category assigned to this body; zero uses the default all-category behavior. |
| Collision | `Collision Mask` | Categories this body may collide with; zero uses the default all-category behavior. |
| Collision | `Collision Group` | Positive forces collision within a group, negative disables it, and zero disables group filtering. |
| Network | `Enable Networked Simulation` | Enables the component's server-authoritative network path. |
| Network | `Max Server Distance` | Relevance radius in cm for extra validation/state work. This is a network and bandwidth budget, not a physics-performance or simulation culling distance. |
| Network | `Replication Interval Seconds` | Minimum time between authoritative state updates for this body. |

`AddImpulse` is Blueprint-callable. Client input is sent as intent; the server validates it against its authoritative simulation and bounded history rather than trusting a client transform.

## Quick start: fast instanced path

Use `ABox3DInstancedBodyActor` for debris, crowds of simple rigid bodies, or any collection where one Unreal actor per body would be unnecessary overhead.

1. Place **Box3D Instanced Body Actor** in the level.
2. Assign `Instance Mesh`.
3. Set `Body Half Extent` and the material/sleep settings.
4. On the authority, call `SpawnBody(World Transform)` for each body and retain the returned stable integer ID.
5. Apply forces with `AddImpulseToBody(Body ID, Impulse)`.

The actor creates one Box3D box per spawned instance and renders them through a `UHierarchicalInstancedStaticMeshComponent`. `SpawnBody` and `AddImpulseToBody` are authority-only. Clients do not create their own simulation; in network mode they receive chunked instance transforms.

### Instanced actor settings

| Category | Setting | Purpose |
| --- | --- | --- |
| Instances | `Instance Mesh` | Static mesh rendered for every instance. |
| Instances | `Body Half Extent` | Half-extents of every simulated box, in cm. |
| Instances | `Density` | Density of every instance in kg/m³. |
| Instances | `Linear Damping` | Linear velocity damping for every instance. |
| Instances | `Angular Damping` | Angular velocity damping for every instance. |
| Instances | `Enable Sleep` | Allows instances to sleep. |
| Instances | `Sleep Threshold` | Sleep speed threshold in cm/s. |
| Network | `Replication Chunk Size` | Maximum body states sent in one replication chunk (1-512). |
| Network | `Replication Chunk Interval Seconds` | Minimum time between chunks (0.001-1 seconds). Lower values improve freshness but spend more network budget. |
| Network | `Interpolate Replicated Instances` | Smooths client HISM transforms instead of snapping to snapshots. |
| Network | `Multiplayer Compatible` | Per-actor switch for replicated network mode. Disable for the local fast path. |
| Network | `Replicated Instance Interpolation Speed` | Client smoothing speed when interpolation is enabled. |

### Local fast path and multiplayer switches

There are two switches for instanced actors:

- **Project Settings > Plugins > Box3D > Networking > Multiplayer Compatible** is the global project policy. It defaults to enabled.
- **Instanced Body Actor > Network > Multiplayer Compatible** is the per-actor override.

The actor uses the network path only when **both** switches are enabled. If either is disabled, the actor turns replication off and updates its local HISM directly. This is intended for single-player or strictly local effects where replication bookkeeping is unnecessary. In one Ryzen 5 7600 measurement, disabling the network path saved approximately **0.04 ms**; treat that as an indicative measurement, not a guaranteed result on every project or platform.

Do not disable either switch for bodies that must be visible and authoritative on multiple machines.

## Static geometry

### Component-based static geometry

Add a `Box3D Body Component`, set `Body Type` to `Static`, and choose `Static Source`. `Auto` prefers complex tri-mesh data when available and otherwise uses simple collision. Static components are created once and do not move.

### Tagged bulk static geometry

Use this path when a level has many static meshes and you do not want to add a `Box3D Body Component` to every actor.

1. Decide on a tag. This example uses `Box3DStatic`.
2. In the level editor, select each actor that should become Box3D collision.
3. In the actor's **Details** panel, open **Actor > Tags**, add `Box3DStatic`, and save the level.
4. Configure the subsystem in `Config/DefaultGame.ini`:

   ```ini
   [/Script/Box3DUnreal.Box3DSubsystem]
   StaticGeometryTag=Box3DStatic
   StaticGeometryFriction=0.6
   StaticGeometryRestitution=0.0
   ```

   `StaticGeometryTag=None` disables the tagged scan. The friction and restitution values are shared by every tagged actor.
5. Start the level with Box3D enabled:

   ```console
   box3d.Enabled 1
   ```

6. When the level loads, or when a streamed sublevel is added, the subsystem finds actors carrying the configured tag and mirrors their collision into the Box3D world as static bodies.
7. Place or spawn a dynamic Box3D body above the tagged geometry to verify the result:

   ```console
   box3d.Spawn 50
   ```

The tag is applied to the **actor**, not to an individual component. The subsystem extracts collision from each tagged actor using its automatic static-geometry path: it uses complex tri-mesh collision when available and otherwise uses simple collision. Tagged geometry is static, so moving or animating the source actor after registration does not move its Box3D body. If the geometry must move, use a `UBox3DBodyComponent` with `Body Type` set to `Kinematic` instead.

Do not add a static `UBox3DBodyComponent` to the same actor while also tagging it unless you intentionally want two collision bodies. For packaged builds, use the baked workflow below instead of relying on runtime tri-mesh extraction.

### Baked collision assets

The bake commandlet creates a `UBox3DCollisionData` asset containing a copy of the selected map's static collision. At runtime, the subsystem loads the asset and creates Box3D static bodies directly from the saved data. This means the packaged game does not need to cook or extract the original meshes at runtime.

#### Step 1: Prepare the map

Before baking, open the map and make sure:

- Every actor that should be included has a `Box3D Body Component` with `Body Type` set to `Static`; **or**
- the actor has the tag you will pass with `-Tag=...`.
- The actors are in the saved map, at their intended transforms and scales.
- Dynamic and kinematic actors are not marked for baking. They are simulated live and are skipped by the commandlet.

When a static component is present, its `Static Source` and `Invert Mesh Winding` settings control how that actor is extracted. Tagged actors without a component use the automatic extraction mode.

#### Step 2: Run the bake commandlet

Run the command from a Command Prompt, PowerShell, or a batch file. Replace the executable and project paths with your Unreal installation and project:

```text
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Projects\MyGame\MyGame.uproject" -run=Box3DBake -Map=/Game/Maps/Foo
```

`-Map` is the Unreal **package name**, not a Windows `.umap` filename. The commandlet loads that map, finds its eligible static actors, extracts their collision, and saves one asset. If no output path is supplied, the default asset is created beside the map as `BC_Foo`.

To bake tagged actors in addition to actors with static components:

```text
-Map=/Game/Maps/Foo -Tag=Box3DStatic
```

To choose an explicit output package for one map:

```text
-Map=/Game/Maps/Foo -Out=/Game/Physics/BC_Foo -Tag=Box3DStatic
```

To bake multiple maps, pass a comma-separated package list. Each map gets its own derived `BC_<MapName>` asset; `-Out` is only used for a single-map bake:

```text
-Map=/Game/Maps/Foo,/Game/Maps/Bar -Tag=Box3DStatic
```

The command reports how many bodies were written. If it reports that no actors or shapes were found, check the map package name, the component `Body Type`, and the tag spelling. Re-run the command whenever the source level geometry, transforms, collision settings, or Box3D version changes.

#### Step 3: Confirm the generated asset

After the command succeeds, return to the editor and locate the generated asset in the Content Browser. For the default command it will be next to the map:

```text
/Game/Maps/BC_Foo
```

For the explicit command it will be:

```text
/Game/Physics/BC_Foo
```

The asset records the source level, bake time, Box3D version, source fingerprint, body count, and whether it contains tri-mesh data. It stores collision data, not render meshes and not dynamic body state.

#### Step 4: Tell the subsystem to load the asset

There are two ways to apply the baked asset at runtime:

1. **Automatic map asset discovery (recommended for the default output):** keep `Auto Discover Baked Collision` enabled. When the current map is `/Game/Maps/Foo`, the subsystem looks beside it for `BC_Foo` and loads it automatically.
2. **Explicit asset list:** add the generated `UBox3DCollisionData` asset to the subsystem's `Baked Collision Assets` array. This is useful when the asset is stored in another folder or when one world must load collision baked from a specific map.

The settings are stored in `Config/DefaultGame.ini`. An explicit configuration looks like:

```ini
[/Script/Box3DUnreal.Box3DSubsystem]
bAutoDiscoverBakedCollision=true
BakedCollisionAssets=/Game/Physics/BC_Foo.BC_Foo
StaticGeometryFriction=0.6
StaticGeometryRestitution=0.0
```

In the editor, these properties are exposed on the Box3D world subsystem settings under **Bulk Static**. If you use explicit assets, use the asset picker rather than typing a path when possible.

#### Step 5: Run and verify

1. Package or launch the project with the generated `UBox3DCollisionData` asset included in cooked content.
2. Open the same map that was baked, or load the map whose collision asset you explicitly configured.
3. Confirm Box3D is enabled:

   ```console
   box3d.Enabled 1
   box3d.DebugDraw 1
   ```

4. Spawn a dynamic test body:

   ```console
   box3d.Spawn 10
   ```

The body should collide with the baked geometry. Baked bodies are instantiated from the asset and have no live source actor, so query hits against them have a null `HitActor`. If the source map changed, bake again and replace or overwrite the old asset.

The baked workflow and tagged runtime workflow can coexist, but avoid loading the same geometry through both paths or it will be duplicated in the Box3D world. Choose either the tag scan or the corresponding baked asset for a given set of static actors.

## World and project settings

The `UBox3DSubsystem` owns one Box3D world per supported Unreal world and advances it at a fixed timestep.

| Category | Setting | Purpose |
| --- | --- | --- |
| Box3D | `Fixed Time Step` | Simulation step size; default is 1/60 second. |
| Box3D | `Sub Step Count` | Box3D solver iterations/substeps per fixed step. |
| Box3D | `Worker Count` | Box3D solver workers; keep at 1 when deterministic server/rollback results are required. |
| Box3D | `Max Frame Time` | Spiral-of-death guard; caps simulation time consumed from one frame. |
| Box3D | `Gravity` | Gravity in Unreal cm/s²; default is `(0, 0, -980)`. |
| Bulk Static | `Static Geometry Tag` | Opt-in tag for runtime bulk static registration. |
| Bulk Static | `Static Geometry Friction` | Shared friction for tagged and baked static geometry. |
| Bulk Static | `Static Geometry Restitution` | Shared restitution for tagged and baked static geometry. |
| Bulk Static | `Baked Collision Assets` | Pre-cooked collision assets to load at world start. |
| Bulk Static | `Auto Discover Baked Collision` | Automatically load the current map's `BC_<MapName>` asset. |
| Network | `Network Rewind Seconds` | Maximum age of server history available for validation. |
| Network | `Network Rewind Max Frames` | Maximum number of history frames retained per body. |

The subsystem simulates on standalone and server authorities. A pure client has no Box3D simulation world; its role is to display replicated state. The master runtime switch is:

```console
box3d.Enabled 0
box3d.Enabled 1
```

Launch with `-DisableBox3D` to start disabled. Toggling the switch tears down or rebuilds the Box3D world and eligible registered bodies, which is useful for A/B comparisons.

## Queries and filtering

The subsystem exposes Blueprint-callable queries in Unreal cm:

- `RaycastClosest` and `RaycastMulti`
- `SphereCast` and `BoxCast`
- `OverlapAABB` (broadphase candidate query)
- `OverlapSphere` and `OverlapBox` (exact overlap tests)

Use `FBox3DQueryFilter` to set a query `Category` and `Mask`. A zero category or mask preserves the default behavior of querying all categories. Queries run against Box3D geometry and therefore return results on standalone/server worlds, not on a pure client.

## Debugging and diagnostics

```console
box3d.DebugDraw 1
box3d.DebugDraw 0
box3d.Spawn 50
box3d.QueryRay
box3d.QueryOverlap
box3d.HashState
box3d.DeterminismTest
box3d.SnapshotTest
box3d.RollbackTest
```

`box3d.Spawn` accepts an optional count. Its helper variables include `box3d.SpawnMesh`, `box3d.SpawnShape` (`0 Auto`, `1 Box`, `2 Sphere`, `3 Capsule`, `4 Convex`), `box3d.SpawnHeight`, `box3d.SpawnForward`, `box3d.SpawnSpread`, and `box3d.SpawnScale`.

## PhysX interaction and limitations

Box3D and PhysX do not solve contacts against each other. A Box3D-driven actor should have PhysX simulation disabled; set the root component's collision profile to **No Collision** if PhysX should ignore it completely. Dynamic and kinematic Box3D actors are made movable and their transforms are driven by the Box3D integration.

Box3D is deterministic for the same inputs and simulation configuration, but changing solver worker count can change constraint partitioning. For network authority and rollback, keep the worker count consistent and normally leave it at `1`.

## Third-party code and license

The Unreal integration is released under the MIT License; see [LICENSE](LICENSE). Box3D is included as a submodule and remains under its own license. See `ThirdParty/box3d/LICENSE` and the [upstream Box3D documentation](https://github.com/erincatto/box3d).
