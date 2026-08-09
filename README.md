# Box3DUnreal

<p align="center">
<img width="640" height="360" alt="box3d_test" src="https://github.com/user-attachments/assets/70152a87-7416-4efd-931f-063f6dff1ec6" />
</p>

## About

Hi, I'm Antonio Lattanzio. I'm a Principal Engineer at **Empty Vessel**, where we are currently working on **DEFECT**, an upcoming multiplayer immersive action game built with Unreal Engine.

🎮 **DEFECT**
[Steam Link](https://store.steampowered.com/app/2470010/DEFECT/)

I'm also the founder of **Mental Drink**, an independent game studio creating original games and experimental technology.

🎮 **Mental Drink Games**
[Steam Link](https://store.steampowered.com/search/?developer=Mental%20Drink)

---

# Overview

**Box3DUnreal** is an Unreal Engine plugin that integrates **Box3D** into Unreal Engine 5.

The goal of this project is to provide a clean and lightweight Unreal-friendly integration of Box3D, making it easier for developers to experiment with and build gameplay systems using a fast and robust physics engine.

> 🚧 **Early Development**
> This plugin is currently focused on the core Box3D integration. APIs, features, and documentation will continue to evolve.

---

# Features

* Box3D integrated into Unreal Engine 5
* Unreal Engine plugin architecture
* C++ and Blueprint API
* Physics events for designers: hit, contact, overlap, sleep/wake
* Box3D world and simulation support
* Lightweight integration layer
* Static mesh simulation support
* Raycasts, overlaps and shape casts against the Box3D world

More features and examples will be added as the project evolves.

---

# Why Box3D?

Box3D, created by **Erin Catto**, is a lightweight and robust physics engine used in many games and simulations.

While Unreal Engine provides Chaos Physics, Box3D can be useful for developers looking for:

* A lightweight physics solution
* Deterministic simulation
* Custom gameplay physics systems
* Experimentation with alternative physics engines

This project aims to make Box3D accessible from Unreal Engine while keeping the integration simple and flexible.

---

# Installation

Clone the repository including submodules:

```bash
git clone --recurse-submodules https://github.com/alattanzio/Box3DUnreal.git
```

Install the plugin either at the project level:

```text
<Project>/Plugins/Box3DUnreal
```

or at the Unreal Engine level:

```text
<UnrealEngine>/Engine/Plugins/Box3DUnreal
```

If installed at the engine level, enable the plugin from the Unreal Editor **Plugins** window if it is not enabled automatically.

![Enable Plugin](https://github.com/user-attachments/assets/3ca74b2c-f0f3-433d-9a43-ebb261496428)

Generate project files and build your Unreal project.

---

# Instructions

## Adding Box3D Simulation

To simulate a mesh using Box3D, add the **Box3DBody Component** to the Static Mesh Actor.

The component will register the mesh with the Box3D simulation and handle physics simulation independently from Chaos Physics.

![Box3DBody Component](https://github.com/user-attachments/assets/5310e80b-a24f-41e4-81c6-d864dd74fd3e)

Alternatively, you can add the **Box3DBody Component** directly from the Actor Components panel in the Outliner.

![Adding Box3DBody Component](https://github.com/user-attachments/assets/8df90aab-c257-4f5e-bfc9-55cdea646532)

---

## Collision Setup

If **Convex** is selected, Box3D will import the Static Mesh simple collision geometry and use it for the Box3D simulation.

![Convex Collision Setup](https://github.com/user-attachments/assets/7a059937-80e6-4d0f-b0cb-04c31312fd2b)

---

## Chaos Physics Compatibility

Chaos Physics remains enabled in Unreal Engine, but the object will **not** be simulated by Chaos.

From Chaos' perspective, the mesh remains static.

If you want Chaos to completely ignore the object, set the Collision Profile to:

**No Collision**

![No Collision Setting](https://github.com/user-attachments/assets/b40c395d-887e-4d4d-8fa0-8e190cb88c42)

![Uploading box3d_test.gif…]()
![Collision Example](https://github.com/user-attachments/assets/760fb108-8c4b-4125-8a48-61d3dc4268a7)

---

## Physics Events

Events work the same way they do in Chaos: tick a checkbox on the **Box3DBody Component**,
then bind the matching event in the actor's Blueprint graph.

| Checkbox | Events |
| --- | --- |
| **Simulation Generates Hit Events** | `On Box3D Hit` |
| **Generate Contact Events** | `On Box3D Begin Contact` / `On Box3D End Contact` |
| **Generate Overlap Events** | `On Box3D Begin Overlap` / `On Box3D End Overlap` |
| **Is Trigger** | turns the body into a trigger volume (never blocks) |
| **Generate Sleep Events** | `On Box3D Sleep` / `On Box3D Wake` |

**Hit** is the one most gameplay wants. It only fires for collisions above a speed threshold
and carries the impact location, normal and closing speed, so you can scale damage, sound or
decals by how hard the impact was. **Contact** fires for every touch however gentle — use it
only when the touch itself is the point.

Two things behave differently from Chaos, both inherited from Box3D:

* **Hit and contact only need the checkbox on one of the two bodies.** A prop can hear about
  hitting level geometry that has no events of its own.
* **Overlap needs it on both.** A trigger is blind to anything without *Generate Overlap
  Events*, so tick it on the trigger *and* on whatever should be detected. A trigger should be
  Static or Kinematic — a Dynamic one never collides, so it just falls out of the level.

Events fire on the server only, since only the server simulates. Replicate the reaction, not
the event.

For a single handler that hears every impact in the level (impact audio, decals), bind
`On Any Box3D Hit` on the **Box3D Subsystem** instead.

---

## Blueprint API

The body component exposes the usual physics verbs: `Add Impulse`, `Add Impulse At Location`,
`Add Force`, `Add Torque`, `Add Angular Impulse`, `Set`/`Get Linear Velocity`,
`Set`/`Get Angular Velocity`, `Set Gravity Scale`, `Set Sleep Enabled`, `Teleport Body`,
`Get Body Mass`, `Is Body Awake`, `Wake Body`.

The subsystem exposes the world: raycasts, overlaps and shape casts, `Get`/`Set Gravity`,
`Apply Radial Impulse`, plus `Is Simulation Authority` — check that before trusting any
Box3D state, because a client has no bodies and everything comes back empty.

---

# Console Commands

## Spawn Box3D Test Object

```console
box3d.Spawn
```

## Debug Rendering

Enable:

```console
box3d.DebugDraw 1
```

Disable:

```console
box3d.DebugDraw 0
```

## Self-Checking Tests

These need no level content and log a pass/fail tally.

```console
box3d.QueryTest
box3d.EventTest
```

---

# Compatibility

Tested with:

* Unreal Engine **5.7.x**
* Unreal Engine **5.8.x**

---

# Current Limitations

This project is currently focused on the core Box3D integration.

Current limitations:

* No Chaos ↔ Box3D physics interaction
* Server-authoritative only — clients display replicated movement, no client prediction yet
* No joints
* The simulation steps on the game thread
* Limited editor tooling

Additional features will be added as the integration evolves.

---

# Roadmap

Rough order of value, not a schedule.

* **Joints.** Box3D has a full joint API and none of it is exposed yet. Doors, hinged debris,
  ragdolls and vehicles currently have to fall back to Chaos, which means they can't share the
  deterministic world. Biggest single gap.
* **Async / off-thread stepping.** The step is a clean unit of work and could run alongside the
  frame with the transform write-back on the game thread. Note the parallelism has to come from
  stepping beside the frame, not from more solver workers — those repartition the constraint
  graph and break determinism.
* **Continuous collision and per-axis motion locks.** One property each on the component.
  CCD matters as soon as anything fast-moving becomes a Box3D body.
* **Landscape height fields.** Tri-mesh collision already works; a real height field would be
  faster.
* **Client-side prediction and rollback.** The snapshot, hash and replay pieces exist and are
  proven headless; wiring them to a live client is the remaining work.
* **Pre-solve contact events** for one-way platforms and conveyors. These run on solver
  threads, so they can only ever be a C++ callback, never a Blueprint event.
* **Example project.**

---

# Contributing

Contributions are welcome!

If you find an issue, have an idea, or want to improve the plugin:

* ⭐ Star the repository
* 🐛 Report bugs
* 💡 Suggest improvements
* 🔀 Submit pull requests

Whether it is improving documentation, adding features, or fixing issues, every contribution is appreciated.

---

# Connect & Support

If you enjoy this project, feel free to follow my work on GitHub or connect on LinkedIn.

I'm also the founder of **Mental Drink**, an independent game studio creating original games and experimental technology.

If you'd like to support independent development, consider wishlisting our games on Steam. Every wishlist helps developers continue creating games and open-source technology.

LinkedIn:
https://www.linkedin.com/in/antoniolattanzio/

🎮 **DEFECT**
[Steam Link](https://store.steampowered.com/app/2470010/DEFECT/)

🎮 **Mental Drink Games**
[Steam Link](https://store.steampowered.com/search/?developer=Mental%20Drink)

---

# Third-Party

This plugin integrates **Box3D**, created by Erin Catto.

Please refer to the official Box3D project for licensing information.

---

# License

The Unreal Engine integration is released under the MIT License.

Box3D remains under its own license.
