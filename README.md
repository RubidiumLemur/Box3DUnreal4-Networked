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
* C++ API
* Box3D world and simulation support
* Lightweight integration layer
* Static mesh simulation support

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

---

# Compatibility

Tested with:

* Unreal Engine **5.7.4**
* Unreal Engine **5.8.1**

---

# Current Limitations

This project is currently focused on the core Box3D integration.

Current limitations:

* No Chaos ↔ Box3D physics interaction
* No multiplayer/network replication
* No Blueprint API yet
* Limited editor tooling

Additional features will be added as the integration evolves.

---

# Roadmap

Planned improvements:

* Physics world management
* Body creation helpers
* Collision shape support
* Unreal Actor / Component integration
* Blueprint support
* Debug visualization
* Example project

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
