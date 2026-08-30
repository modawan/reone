# E-series identity, save, and runtime lifecycle architecture

**Status:** Canonical current specification  
**Applies to:** The completed L-series resource loader and E-series save/runtime implementation through C5  
**Historical sources:** `Reone_E-Series_Save_Load_Design_Baseline_UPDATED.md` and `reone_l_series_loader_design_v1.0.md` in the project design archive

This document is the authoritative current E-series architecture specification. The historical E baseline remains the evidence and roadmap record, but its prospective stage descriptions are superseded by this document where they describe current behavior. The L-series design remains authoritative for Odyssey resource selection, lookup buckets, mount planning, and source ownership; this document summarizes only the L contracts needed to understand E.

The architecture targets K1/K2 observable behavior and content compatibility. It does not reproduce Odyssey filesystem staging, unsafe pointer lifetimes, or destructive failure behavior merely for implementation fidelity.

## 1. The eight engine laws

1. Resources have explicit owners; source lifetime is independent of lookup priority.
2. Every serialized object reference has an explicit identity domain.
3. Logical roster identity is a PartyTable slot, explicitly bound to a runtime representation.
4. A creature may survive an Area, but its Area-runtime residency may not.
5. Runtime identity ends at semantic destruction, even if C++ storage remains.
6. A fallible object or owned graph is constructed before it is published.
7. Enough destination resources and structure are prepared to reject avoidable failure before sacrificing a good world.
8. Authored gameplay begins only after the irreversible publication boundary; it is retired coherently on failure, not transactionally rolled back.

These laws are orthogonal. A saved ID is not a runtime ID, a roster slot is not either ID, an Area attachment is not object existence, and a `shared_ptr` is not gameplay liveness.

## 2. Architectural phases

The implementation preserves the original E macro model while refining its commit boundary:

```text
durable save or current working state
        |
        v
resource and structural preparation
        |
        v
source exit and snapshot preparation, when transitioning
        |
        v
irreversible resource/session publication
        |
        v
runtime and Party reconstruction
        |
        v
saved-reference and runtime-state binding/publication
        |
        v
playable-session publication
```

These are semantic phases, not a requirement for separate threads or a literal state-machine class. The current loader is synchronous and main-thread-owned.

There are three nested commit scopes:

| Scope | What is staged | Commit meaning |
|---|---|---|
| Save working state | Indexed `SaveWorkingState` and exact slot metadata | Replace the committed save session |
| Module resources | L source index, mount plan, and decoded core structural records | Replace `ActiveModule` and `ActiveModuleState` sources |
| Runtime object graph | One object or owned child graph | Publish exact live objects and retire the old graph |

There is deliberately no private duplicate of the complete Game, Party, gameplay registry, or scene. C5 is a bounded structural-preparation boundary, not a database transaction over a gameplay world.

## 3. L resource ownership and lookup

E composes with L; it does not own a second resource architecture.

### 3.1 Lookup is independent of lifetime

Raw lookup uses the L five-bucket Odyssey order:

```text
LooseDirectory
> EncapsulatedClass1
> ResourceImage
> EncapsulatedClass2
> KeyBif
```

Newer registration wins only within a bucket. Primary module selection, mount phase, lookup priority, and source lifetime remain distinct concepts. A winning raw UTC/GIT/ARE does not by itself restore a runtime object.

### 3.2 Resource owners

| Owner/state | Purpose | Lifetime |
|---|---|---|
| `Global` | Installation KEY/BIF, override, patch, and other process/game sources | Resource-system shutdown or reinitialization |
| Save session / `SaveSlot` | Exact `SaveSlotDescriptor`, loose metadata view, and indexed `SaveWorkingState` | Save replacement, new game, or session abandonment |
| `ActiveModule` | Module support/static sources such as adjuncts and module archives | Every module transition, including failed committed loads |
| `ActiveModuleState` | Selected saved/current module state | Every module transition and save replacement |
| `TemporaryDiscovery` | Candidate sources mounted only while preparing core records | Preparation completion or failure |

The durable outer `SAVEGAME.sav` is represented by `SaveSessionState`/`SaveWorkingState`; it is not made the global live resource universe. Saved module candidates are handed to the normal L selection and mount policy.

### 3.3 Prepared module resources

`PreparedModuleLoad` owns the selected source index and L mount plan and carries decoded IFO, ARE, and GIT records. Preparation temporarily mounts the candidate plan with `TemporaryDiscovery` ownership, excludes the outgoing `ActiveModule` and `ActiveModuleState` sources from candidate lookup, validates IFO/ARE/GIT and the entry LYT, then rolls the temporary mounts back.

`commitModuleLoad` clears the two old module owners and mounts the already selected plan with its real owners and buckets. An external resource changing between preparation and commit is a post-commit failure; prepared source bytes are not frozen into a second resource store.

## 4. Serialized identity

An ObjectId-shaped field is meaningful only with a `SerializedIdentityContext`.

| Domain | Authority | Reference interpretation | Runtime publication |
|---|---|---|---|
| `ModuleGraph` | One authoritative saved module-instance graph | IDs refer within that graph, including structural Module slot `0` | Saved ID is registered to the independently allocated live runtime object |
| `DetachedRecord` | One owner-/record-local serialized record, such as `pc.utc`, `AVAILNPCn`, `AVAILPUPn`, or party inventory | Local IDs and reference-shaped values never silently bind through the active module graph | Materialization policy may publish supported detached state; it does not invent a module identity |
| `Template` | No authoritative runtime-instance namespace | ObjectId-shaped template data is not a saved module reference | A fresh runtime object receives an independent runtime ID |

### 4.1 Saved graph rules

- Module saved IDs are graph-local, not process-global.
- Saved and runtime IDs may differ arbitrarily.
- Numeric equality across domains proves nothing.
- The saved graph owns `saved ID -> live object` and `live object -> canonical saved ID` translation.
- Structural Module slot `0` is a graph reference target, not an ordinary runtime allocation rule.
- Inbound references are parsed first, bound only after the graph is materialized, and published only after required bindings are complete.
- Export translates live references to the destination graph's canonical saved IDs exactly once.
- The saved-graph generation scopes parsed/bound references; retirement increments the generation and drops mappings and aliases.
- Missing, deleted, or out-of-graph mandatory references fail closed. Explicitly preserved unsupported payloads remain shadows and are not treated as executable state.

Known reference-bearing values—including VM objects, Talent items, Effect creator/object parameters, actions, events, delayed script situations, perception records, and saved object fields—use the same context-aware conversion and binding rules. Live Effects in saved continuations are converted to a save-facing value before reference normalization.

Detached records may contain real effects/actions. Their parse context remains detached, so coincident module numbers cannot capture them. The Party materialization policy decides which supported detached runtime state becomes live.

## 5. Logical Party and roster identity

The canonical companion identity is:

```text
(RosterKind, PartyTable slot)
```

`RosterKind::Npc` and `RosterKind::Puppet` are separate namespaces. K1 has nine NPC slots and no puppet namespace; K2 has twelve NPC slots and three puppet slots.

Party owns these distinct concepts:

- logical availability;
- selectability;
- detached persistent `AVAILNPCn`/`AVAILPUPn` representation;
- an optional transient runtime-creature binding;
- active membership and leader ordering;
- controlled-NPC state;
- canonical PC storage and current player representation;
- K2 puppet assignment.

The runtime binding is explicit and replaceable. Party operations bind, clear, lazily spawn, kill, add, remove, or rebuild it. Adding an active member establishes the corresponding binding. Semantic destruction clears the exact matching binding but does not silently change logical availability.

Tag is an authored lookup/association value used by explicit retail-compatible routines where their contract calls for it. It is not generic roster identity. Therefore all of these may legally coexist:

- an available NPC record tagged `remote`;
- an available K2 puppet record tagged `remote`;
- an ordinary GIT creature tagged `remote`.

GIT state is a module graph; a detached roster snapshot is a save-wide Party record. Neither representation is opportunistically merged into the other. An explicit Party operation may bind a module creature to a slot, after which roster serialization may snapshot that bound creature into the detached record.

Pointer continuity and runtime ObjectId continuity are not logical continuity.

## 6. Area residency

Area residency is distinct from both logical Party identity and runtime-object existence.

### 6.1 Full Area departure

Full departure ends the current Area lifetime while the Area, Rooms, Triggers, and Pathfinder are still alive. For retained session creatures it retires:

- Area collections and scene attachment;
- Room membership and raw Room pointers;
- Trigger tenancy;
- Pathfinder-owned path handles and movement derivation;
- blocking-door/navigation bindings;
- combat targets and transient combat state tied to the outgoing Area;
- perception seen/heard bindings to outgoing objects;
- live actions and delayed execution after the outgoing snapshot has captured supported durable state;
- Area-bound presentation/reference components of effects.

Durable stats, HP, locals, inventory/equipment, logical roster state, and persistent mechanical effect semantics survive. Effect-derived state is preserved while outgoing Area/presentation bindings retire.

The required source order is:

```text
OnExit -> snapshot durable state -> full Area retirement -> Module destruction
```

### 6.2 Same-Area reposition or control transfer

`SwitchPlayerCharacter`, Party repositioning, and selection/control transfer do not end the Area lifetime. They preserve action queues, delayed commands, effects, saved-graph generation, combat/perception state unless a specific gameplay rule changes it, and logical roster bindings. They adjust only control, formation, transform, Room calculation, and scene residency needed by the same Area.

The separate APIs make module-boundary retirement unavailable as a convenient reposition helper.

## 7. Runtime semantic liveness

Every gameplay Object is in one of four states:

| State | Meaning |
|---|---|
| `Constructing` | Storage exists but is not yet a published gameplay object |
| `Live` | This exact runtime incarnation is published and gameplay-addressable |
| `Retired` | Gameplay existence ended; storage may remain |
| `Presentation` | Non-authoritative display object outside the gameplay registry |

A live object has one runtime ObjectId and one runtime incarnation. The registry is the authoritative publication surface, but non-owning references must also prove that they still address the same live incarnation.

`RuntimeObjectRef<T>` therefore stores a weak storage reference plus the bound incarnation. Resolution succeeds only if storage remains, the Object is `Live`, and the incarnation still matches. It never resolves by numeric ID, so a later object reusing the number cannot revive a stale reference.

This runtime-incarnation domain is separate from the saved-graph generation and from the runtime-session generation. Saved identity does not own runtime storage, and runtime liveness does not confer persistence.

### 7.1 Semantic destruction

Semantic destruction performs these logical steps:

1. retire children before their owner where an ownership graph exists;
2. mark the exact incarnation `Retired`;
3. clear exact Party runtime bindings and saved-ID mappings/aliases;
4. remove the exact pointer-guarded registry entry;
5. release owner edges as appropriate;
6. let remaining presentation or diagnostic holders release C++ storage later.

Registry removal cannot unregister a newer object that happens to use the same number. A retained strong pointer cannot restore gameplay liveness.

## 8. Runtime-object ownership and publication

Owning edges and non-owning gameplay references are different types of relationship.

- Creature inventory, equipment, placeable/store contents, and Party inventory form owned runtime-object graphs.
- Equipment and inventory use one ownership disposition for an Item; transfer removes the old edge before publishing the new one.
- Displaced equipment moves to the defined inventory owner or is finalized.
- A stack merge finalizes the consumed object immediately.
- Nested children are retired before their owner.
- Presentation clones never take ownership of authoritative gameplay Items.

Fallible GFF/blueprint object construction uses object-graph staging. Candidate objects and saved-ID maps stay outside the live registry until deserialization and validation succeed. Graph replacement discovers the old closure and builds the new closure before a `noexcept` owner swap; publication then merges prebuilt registry nodes and retires the obsolete graph. Failure before the owner swap leaves the prior graph live.

This contract is generic over `ownedRuntimeObjects()` rather than a central Creature/Item type list.

## 9. Presentation isolation

Scene and GUI ownership do not create gameplay identity.

- GUI previews use `Presentation` objects and presentation-only IDs.
- A character preview clones equipment into presentation Items; it never equips or re-owns the player's Item.
- Presentation objects are absent from the gameplay registry and saved identity maps.
- Scene attachment is an Area/presentation relationship and ends on detachment even if model storage remains.
- Retaining a node or preview after semantic destruction cannot make its Object executable or resolvable as gameplay state.

Destination scene construction and attachment currently occur after the destination commit. They are not a privately staged duplicate scene.

## 10. Destination preparation and publication

C5 implements the minimum useful guarantee:

> Enough destination resource and structural state is prepared and validated to reject avoidable structural failure before sacrificing the authoritative session.

It does not privately construct a complete destination gameplay world. Specifically:

### Prepared before commit

- exact save-slot identity and archive index;
- mandatory save metadata and global-variable records;
- optional supplied Party/inventory records decoded for later use;
- destination L source discovery and mount plan;
- IFO, ARE, GIT, and LYT availability and structural parsing;
- Party slot/member range and duplication constraints;
- authoritative saved graph ObjectId claims and allocator cursor constraints where the destination declares a saved module graph;
- source module snapshot candidate during an ordinary transition.

### Deliberately post-commit

- authoritative Party mutation and detached-creature materialization;
- runtime registry population and nested graph construction;
- Module/Area/Room and scene construction;
- map, GUI, music, and other presentation changes;
- saved action/effect/event binding and publication;
- authored spawn, `OnLoad`, and `OnEnter` behavior.

C4 makes each object/owned graph correct at this post-commit boundary. A failure there coherently retires the partial destination and lands on the Main Menu; it does not resurrect an old world or claim arbitrary gameplay rollback.

## 11. Actual transition pipelines

### 11.1 Ordinary module transition

```text
prepare destination L plan and validate IFO/ARE/GIT/LYT
    -> source Area OnExit
    -> build frozen source-module snapshot / candidate SaveWorkingState
    -> if re-entering the same module, reprepare against that snapshot
    -> prepare loading-screen presentation
    -> COMMIT STARTS
    -> retire source Area runtime and ordinary source runtime objects
    -> retire source saved graph and Module
    -> commit destination ActiveModule/ActiveModuleState resources
    -> adopt the source SaveWorkingState candidate
    -> clear/rebuild post-commit UI and scene state
    -> establish destination identity context
    -> construct Module/Area/GIT/nested object graphs
    -> run fresh-module spawn scripts when applicable
    -> establish a default Party when needed
    -> run Module OnLoad
    -> place Party and run Area OnEnter
    -> bind and publish saved references/actions/effects/events
    -> start music and publish Screen::InGame
```

The initial preflight occurs before source `OnExit`. The snapshot is adopted only after module resource publication succeeds. Re-entering the source module is the special case that must reprepare against the newly captured snapshot.

### 11.2 Disk save load

```text
exact SaveSlotDescriptor selected by discovery
    -> prepareGameLoad: build unpublished SaveSessionState
    -> decode/validate NFO and GLOBALVARS
    -> decode supplied PartyTable and inventory records
    -> prepare destination L plan and validate IFO/ARE/GIT/LYT
    -> validate Party and authoritative saved-graph claims
    -> COMMIT: reset/retire old runtime session
    -> commitGameLoad: publish exact save session
    -> commitModuleLoad: publish destination module resources
    -> restore reputation, globals, custom tokens, saved namespace
    -> restore Party, PC/controlled representation, and inventory
    -> construct Module/Area/GIT/nested runtime graphs
    -> run Module OnLoad
    -> place Party and run Area OnEnter
    -> bind and publish saved references/actions/effects/events
    -> start music and publish Screen::InGame
```

The exact `SaveSlotDescriptor` is retained from enumeration through preparation and commit. An installed-module fallback remains permitted when a save has no archived current-module graph.

### 11.3 Same-Area control transfer

```text
resolve canonical PC or explicit roster slot
    -> establish the explicit Party runtime binding/control state
    -> transfer the outgoing leader transform to the incoming controlled actor
    -> recalculate same-Area placement/camera state
```

There is no snapshot, resource activation, saved-graph retirement, or full Area retirement.

### 11.4 Full runtime-session retirement

```text
mark session non-playable and advance runtime-session generation
    -> stop session gameplay/presentation controllers
    -> release GUI-held runtime references
    -> retire retained Party Area residency while Area owners exist
    -> clear combat and transient Party runtime bindings
    -> clear authoritative scene roots and Module ownership
    -> semantically retire every exact registered runtime incarnation
    -> retire saved graph/generation and reset runtime allocators
    -> clear save-wide mutable systems in resetGame
    -> retire save/module resource ownership through L
```

Full session retirement is stronger than ordinary module retirement; ordinary travel preserves save-wide and logical Party state.

## 12. Authored-script boundary

The loader is not a transaction over arbitrary NWScript.

| Hook | Boundary |
|---|---|
| Source Area `OnExit` | Runs after initial destination structural preflight but before source snapshot and irreversible commit |
| Fresh-object spawn scripts | Run after destination runtime structure is authoritative |
| Module `OnLoad` | Runs after Module/Area/GIT construction and publication |
| Area `OnEnter` | Runs during Party placement after `OnLoad` |

Structural preparation executes no destination-authored script. Once destination scripts begin, the old world is intentionally unrecoverable. A script exception or later post-commit failure causes coherent destination/session retirement and a deliberate Main Menu state.

Source `OnExit` is a known weaker boundary: it runs against the authoritative source and arbitrary script mutation is not rollbackable if snapshot creation or same-module repreparation subsequently fails. The design rejects a generic script rollback journal as disproportionate and semantically misleading.

## 13. Failure semantics

| Failure phase | Current world preserved? | Prepared state discarded? | Terminal behavior |
|---|---:|---:|---|
| Save cannot open/index | Yes | Yes | Error propagates; current screen/world remains |
| Destination module resources missing | Yes, before commit | Yes | Ordinary transition returns failure; disk load rejects before reset |
| IFO/ARE/GIT/LYT structurally invalid | Yes, before commit | Yes | Same as missing resources |
| Pre-publication Party or saved-ID validation fails | Yes | Yes | Load rejected before old-session retirement |
| Source `OnExit` throws | Runtime remains, but prior script side effects may exist | Yes | Ordinary transition returns failure |
| Source snapshot or same-module repreparation fails | Runtime remains; arbitrary `OnExit` mutation is not rolled back | Yes | Ordinary transition returns failure |
| Loading-screen setup fails before commit | Yes | Yes | Previous screen restored |
| Prepared resource plan changes/fails at commit | No; retirement has started | Best-effort retirement | Main Menu with surfaced error |
| Post-commit Party/runtime/nested-object construction fails | No | Partial destination retired | Main Menu, never a black `Screen::None` world |
| Authored spawn/`OnLoad`/`OnEnter` fails | No | Arbitrary gameplay is not rolled back | Destination retired; Main Menu |

No stable boundary intentionally depends on a later registry or resource cleanup sweep.

## 14. Lifetime hierarchy

| Concept | Identity/lifetime authority | May alias? | Ends when |
|---|---|---:|---|
| Resource source | L `ResourceOwner` plus source identity | Same raw key may exist in several buckets/sources | Owner retires |
| Save session | Exact `SaveSlotDescriptor` and `SaveSessionState` | No active-session alias | Save replacement, new game, or abandonment |
| Saved graph | `SerializedIdentityContext(ModuleGraph)` plus saved-graph generation | Several serialized references may target one live object within the graph | Module saved graph retires |
| Detached record | `SerializedIdentityContext(DetachedRecord)` and record owner | Local numbers may repeat in other records | Record/materialization context ends |
| Logical roster member | `(RosterKind, PartyTable slot)` | Serialized representations may coexist | Logical Party state changes or session ends |
| Runtime incarnation | Runtime ObjectId, exact Object, incarnation, and registry | Numeric ID may later be reused; incarnation may not | Semantic retirement |
| Area residency | Area/Room/Trigger/Pathfinder and scene attachment | One live creature has at most one current residency | Full Area departure |
| Presentation | GUI/scene owner and presentation-only object ID | May display copied gameplay data | Presentation closes or detaches |

## 15. Required invariants and forbidden shortcuts

The implementation must preserve these invariants:

- serialized identity, runtime identity, and roster identity are independent;
- references bind only inside their declared graph/domain lifetime;
- one logical roster slot has at most one explicit live binding;
- one semantically live object has one exact registry entry;
- Area-owned state ends before Area owners die;
- nested ownership replacement is all-old or all-new at its owner boundary;
- semantic destruction immediately ends runtime resolvability;
- pre-commit structural rejection does not replace a good world;
- post-commit failure yields a coherent terminal state;
- K1/K2 differences are policy/data, especially roster and puppet cardinality.

The following are architectural anti-patterns:

- inferring generic roster identity from Tag or ResRef;
- inferring identity from equal saved, detached, or runtime ObjectIds;
- retaining Room, Trigger, Pathfinder, combat, or perception state across Area departure;
- treating `shared_ptr` storage as gameplay liveness;
- registering a fallible object before deserialization/validation;
- globally publishing candidate objects and relying on cleanup after ordinary construction failure;
- using one ambiguous “unload Party” operation for departure and repositioning;
- using registry membership as persistence identity;
- building rollback machinery for arbitrary authored scripts;
- creating save-loader shadow versions of the complete Party, registry, scene, or engine.

## 16. General-engine and future suitability

These are general ownership/identity concepts, not KOTOR-save special cases.

- Forward gameplay uses runtime liveness, Area residency, and ownership without requiring save/load.
- A new runtime object type participates by defining construction/deserialization, owned children, saved references, and optional Area/presentation behavior; central lifecycle code need not enumerate the type for retirement.
- A new equipment slot changes equipment schema/rules and serialization, not identity or registry semantics.
- A new content set or Retwo game can provide different resource/Party policies without inheriting Odyssey filesystem staging.
- A future incremental main-thread loader can pause at resource preparation, structural validation, commit, reconstruction, and playable publication boundaries.
- True concurrent/background preparation still requires isolation or synchronization around the temporarily mounted shared resource view.
- Multiplayer can add a network/replication identity domain beside saved, roster, and per-process runtime identities. Clients need not reproduce server runtime or saved IDs.

The architecture does not claim async execution or multiplayer implementation today.

## 17. Design rationale and complexity assessment

The minimum concepts are: L resource ownership; serialized identity domain; logical roster slot; runtime incarnation/liveness; Area residency; owned object graph; and destination preparation/publication. Presentation is a separate ownership surface, not another gameplay identity.

These concepts are independent rather than duplicate mechanisms:

- saved-graph generation prevents serialized-reference lifetime leaks;
- runtime incarnation prevents dead-storage/runtime-ID aliasing;
- Area retirement ends residency without killing a retained object;
- Party binding preserves logical identity without relying on either ID.

Normal forward loading remains:

```text
prepare -> construct/validate -> publish -> enter gameplay
```

The design deliberately rejected private duplicates of major subsystems, a second registry, arbitrary rollback journals, and script transactions. Consequently, some post-commit runtime failures go to Main Menu rather than preserving the old world. That is a smaller and more truthful guarantee.

Save/load imposes explicit identity and lifetime contracts on runtime systems, but those contracts also make destruction, GUI previews, object references, inventory transfer, and ordinary module travel safer. No C1-C4 mechanism is redundant with C5.

## 18. Relationship to the original E design

### Unchanged

- Durable save, candidate working state, committed working state, active module resources, runtime reconstruction, and playable publication remain separate concepts.
- L primary selection, buckets, ownership, discovery, and rollback remain inputs to E rather than being duplicated.
- Raw resource visibility remains distinct from runtime restoration.
- Ordinary module transitions and full session replacement remain distinct lifetimes.
- Behavioral/content fidelity takes priority over museum restoration of Odyssey internals.
- Phase boundaries remain suitable for later incremental orchestration without making E an async project.

### Clarified

- `ModuleGraph`, `DetachedRecord`, and `Template` now define serialized reference authority.
- PartyTable slot identity, transient runtime binding, active membership, and controlled-PC topology are separate.
- Area residency and semantic object existence are separate lifetimes.
- Registry liveness and C++ storage lifetime are separate.
- Nested ownership and object-level staged publication have explicit guarantees.
- Playable publication and post-commit authored gameplay have an explicit failure policy.

### Corrected or refined

- Preflight now extends beyond opening `SAVEGAME.sav` to core destination module structure before old-session retirement.
- Source working-state mutation is adopted after destination resource publication rather than merely when a snapshot was produced.
- Full Area retirement is not used for same-Area control switching.
- Runtime references no longer assume registry removal destroys all storage.
- The experimental automatic Tag-based GIT/roster reconciliation was rejected; it was not an original E macro principle.
- The final design does not promise a private whole-world transaction. Its bounded preparation guarantee is narrower and simpler.

The evidence supports the conclusion that E's macro lifecycle design remained sound; implementation and correction work supplied identity, roster, residency, liveness, ownership, and publication contracts that the original design left under-specified.

## 19. Remaining Loader roadmap boundaries

- **F:** migrate remaining consumers of the Legacy resource container/backend to the common loader contract.
- **G:** delete the legacy resource architecture after migration and parity validation.
- **H/audit:** optional final enumeration, diagnostics, and cleanup once the cutover is complete.

E must not absorb those backend-migration tasks. Also separate are incremental durable-data/runtime-component refactoring of `GameObject`, actual async/incremental execution, deep multiplayer/replication design, and any future metadata-driven registration of new GIT object classes.

## 20. Consolidation observations for independent audit

The following are not silently resolved by this specification. They are explicit review targets for the frozen implementation:

1. `validatePreparedDestination` treats an installed-module fallback with `Mod_IsSaveGame == false` as non-authoritative, but `resolveModuleLoadContext(initialSaveRestore=true, ...)` currently selects `InitialSaveRestore`, and `restoresSavedWorld` then reconstructs it with a `ModuleGraph` context. The intended fallback domain and the actual reconstruction path require independent confirmation.
2. The actual pipeline runs Module `OnLoad` and Area `OnEnter` before `bindSavedRuntimeState()` and `publishSavedRuntimeState()`. Independent review must decide whether retail/content semantics require entry scripts to observe fully bound saved actions/effects/references or whether this ordering is deliberate.
3. Preparation temporarily mutates the shared resource container under `TemporaryDiscovery` and then rolls it back. This is correct for the current synchronous main-thread loader but is not itself thread-safe async isolation.
4. Prepared resource plans retain source identity/plan and decoded core GFFs, not immutable copies of all later resources. External mutation between preparation and commit follows the terminal post-commit policy.
5. Source `OnExit` mutation cannot be rolled back if snapshot creation or same-module repreparation later fails; this is a documented deliberate limitation, not a claimed transaction.

These observations do not amend production behavior. They identify where the normative intent and the exact implementation deserve independent architectural scrutiny.
