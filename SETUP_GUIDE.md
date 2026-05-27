# LOOPED — Setup Guide

## What's Already Done

All C++ source code for the POC prototype has been written:
- Player Character (FPS controller, movement, sprint, jump)
- Passive Stack Component (6-slot tag-based system with evolution)
- Weapon Holder Component (melee + hitscan, Branch as starting weapon)
- Enemy Base (data-driven enemies with health, damage, death)
- Slo-Mo Manager (automatic trigger queue system)
- GAS Foundation (Attribute Set, Ability System Component)
- Game Mode (run loop, room clear tracking)
- Data Structures (weapon, card, enemy data table definitions)

## What You Need To Do

### Step 1: Open the Project in UE5.7

1. Make sure Unreal Engine 5.7.4 is installed via the Epic Games Launcher
2. In the Epic Games Launcher, click "Launch" next to UE 5.7
3. When the Unreal Project Browser opens, click **"Browse"**
4. Navigate to: `C:\Users\sahar\Desktop\Sahar_playground\Looped\`
5. Select the `Looped.uproject` file
6. Click **Open**

The editor will ask to compile the C++ code. Click **Yes**.

If it asks about generating Visual Studio project files, click **Yes**.

### Step 2: If Compilation Fails

Don't panic. Come back to Claude Code and tell me the error messages. I'll fix them. This is normal for first compilation — there may be small issues to resolve.

### Step 3: Create the Test Level

Once the editor opens:

1. File → New Level → Empty Level
2. Save as: `Content/Maps/L_TestRoom`
3. Add a floor: Place Actors → Basic → Cube, scale to (20, 25, 0.1) for a 20m x 25m room
4. Add light: Place Actors → Lights → Directional Light
5. Add a Player Start: Place Actors → Basic → Player Start
6. Add a Nav Mesh Bounds Volume: Place Actors → Volumes → Nav Mesh Bounds Volume, scale to cover the room
7. Hit Play (green button or Alt+P) to test movement

### Step 4: Come Back to Claude

Once you can run around the test room, come back and we'll:
- Set up the Input Actions (IA_Move, IA_Look, etc.) in the Editor
- Create the Blueprint subclass (BP_LoopedCharacter) with input mappings
- Create Data Tables for weapons, cards, and enemies
- Spawn test enemies
- Wire up the passive card system with visual feedback

## File Structure

```
Looped/
├── Looped.uproject              ← Open this in UE5
├── Source/
│   ├── Looped/                  ← All C++ code
│   │   ├── Core/                ← GameMode, GameInstance
│   │   ├── Player/              ← Character, Components
│   │   ├── Weapons/             ← (future)
│   │   ├── Cards/               ← (future)
│   │   ├── Enemies/             ← EnemyBase
│   │   ├── SloMo/               ← SloMoManager
│   │   ├── GAS/                 ← Ability System, Attributes
│   │   ├── Data/                ← Data structs
│   │   └── Looped.Build.cs      ← Module config
│   ├── LoopedTarget.Target.cs
│   └── LoopedEditorTarget.Target.cs
├── Config/
│   ├── DefaultEngine.ini
│   ├── DefaultGame.ini
│   └── DefaultInput.ini
└── Content/                     ← Assets go here (created in Editor)
```
