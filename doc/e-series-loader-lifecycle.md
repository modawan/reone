# E-series identity, save, and runtime lifecycle architecture

**Status:** Canonical current specification  
**Applies to:** The completed L-series resource loader and the final corrected E-series save/runtime implementation
**Historical sources:** `Reone_E-Series_Save_Load_Design_Baseline_UPDATED.md` and `reone_l_series_loader_design_v1.0.md` in the project design archive

This document is the authoritative current E-series architecture specification. The historical E baseline remains the evidence and roadmap record, but its prospective stage descriptions are superseded by this document where they describe current behavior. The L-series design remains authoritative for Odyssey resource selection, lookup buckets, mount planning, and source ownership; this document summarizes only the L contracts needed to understand E.

The architecture targets K1/K2 observable behavior and content compatibility. It does not reproduce Odyssey filesystem staging, unsafe pointer lifetimes, or destructive failure behavior merely for implementation fidelity.

## 1. The eight engine laws

1. Resources have explicit owners; source lifetime is independent of lookup priority.
2. Every serialized object reference has an explicit identity domain.
3. Logical roster identity is a PartyTable slot, explicitly bound to a runtime representation.
4. A creature may survive an Area, but its Area-runtime residency may not.
5. Runtime identity ends at semantic destruction, even if C++ storage remains.
6. A fallible individual object or owned graph becomes gameplay-live only after its own construction, identity, and ownership are coherent.
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
authored destination entry behavior
        |
        v
playable-session publication
```

These are semantic phases, not a requirement for separate threads or a literal state-machine class. The current loader is synchronous and main-thread-owned.

There are three distinct commit scopes:

| Scope | What is staged | Commit meaning |
|---|---|---|
| Save working state | Indexed `SaveWorkingState` and exact slot metadata | Replace the committed save session |
| Module resources | L source index, mount plan, and decoded core structural records | Replace `ActiveModule` and `ActiveModuleState` sources |
| Runtime object graph | One object, an Area-owned construction graph, or an owned child graph | Publish exact live objects and retire obsolete graph members |

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

`PreparedModuleLoad` owns the selected source index and L mount plan and carries decoded IFO, ARE, and GIT records. Preparation temporarily mounts the candidate plan with `TemporaryDiscovery` ownership, excludes the outgoing `ActiveModule` and `ActiveModuleState` sources from candidate lookup, validates IFO/ARE/GIT and the entry LYT, then rolls the temporary mounts back. Production code can obtain a structurally validated value only through `ResourceDirector`; decoded-record construction used by tests is isolated in a test fixture factory.

`commitModuleLoad` clears the two old module owners and mounts the already selected plan with its real owners and buckets. An external resource changing between preparation and commit is a post-commit failure; prepared source bytes are not frozen into a second resource store.

## 4. Serialized identity

An ObjectId-shaped field is meaningful only with a `SerializedIdentityContext`.

| Domain | Authority | Reference interpretation | Runtime publication |
|---|---|---|---|
| `ModuleGraph` | One authoritative saved module-instance graph | IDs refer within that graph, including structural Module slot `0` | Saved ID is registered to the independently allocated live runtime object |
| `DetachedRecord` | One owner-/record-local serialized record, such as `pc.utc`, `AVAILNPCn`, `AVAILPUPn`, or party inventory | Local IDs and reference-shaped values never silently bind through the active module graph | Materialization policy may publish supported detached state; it does not invent a module identity |
| `Template` | No authoritative runtime-instance namespace | ObjectId-shaped template data is not a saved module reference | A fresh runtime object receives an independent runtime ID |

A disk save session and a saved module world are independent facts. `GIT.UseTemplates` is the world-representation discriminator used by the loader:

- `UseTemplates == 0` identifies an authoritative persisted module graph and therefore a `ModuleGraph` context;
- a template GIT identifies a fresh installed world even when the surrounding operation restores a disk save session.

The latter is the normal retail transition-autosave topology when the save contains save-wide state but no archived current-module graph. Restoring the session never promotes template-local numbers into saved module identities.

### 4.1 Saved graph rules

- Module saved IDs are graph-local, not process-global.
- Saved and runtime IDs may differ arbitrarily.
- Numeric equality across domains proves nothing.
- The saved graph owns `saved ID -> live object` and `live object -> canonical saved ID` translation.
- Structural Module slot `0` is a graph reference target, not an ordinary runtime allocation rule. Its mapping is staged with publication of the runtime Module incarnation and retires with that saved graph.
- Saved Area identity participates in the same staged graph publication. Template Areas and GIT objects receive no `ModuleGraph` identity.
- Inbound references are parsed first, bound only after the graph is materialized, and published only after required bindings are complete.
- Export translates live references to the destination graph's canonical saved IDs exactly once.
- The saved-graph generation scopes parsed/bound references; retirement increments the generation and drops mappings and aliases.
- Missing, deleted, or out-of-graph mandatory references fail closed. Explicitly preserved unsupported payloads remain shadows and are not treated as executable state.

Known reference-bearing values—including VM objects, Talent items, Effect creator/object parameters, actions, events, delayed script situations, perception records, and saved object fields—use the same context-aware conversion and binding rules. Live Effects in saved continuations are converted to a save-facing value before reference normalization.

Detached records may contain real effects/actions. Their parse context remains detached, so coincident module numbers cannot capture them. The Party materialization policy decides which supported detached runtime state becomes live.

### 4.2 World clock and timed state

Reone's canonical runtime clock is absolute simulation milliseconds. Retail saved module state decomposes it into `Mod_PauseDay` and `Mod_PauseTime`; `Mod_MinPerHour` defines the resulting world-day length and does not accelerate elapsed simulation time. Snapshot output writes the retail pair, normalizes `Mod_PauseTime` below one world day, and removes the erroneous historical Reone fields `Mod_CalendarDay` and `Mod_TimeOfDay` from the merged shadow.

Reading retains a narrow backward fallback to that historical Reone pair only when neither retail pause field is present. Template-world transition autosaves instead seed the clock from `AUTOSAVEPARAMS.TIME_PAUSEDAY` and `TIME_PAUSETIME`. Saved events, delayed commands, and effect expiry use the reconstructed absolute clock, so their original due schedule survives restoration or supported ordinary-travel continuation.

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

Availability, binding, membership, and runtime existence are separate authorities:

- changing availability changes logical Party eligibility only;
- clearing or replacing a binding changes the slot's current representation;
- adding or removing active membership changes team composition;
- explicit semantic destruction ends a runtime incarnation and clears an exact matching binding.

Party does not own every creature merely because it is bound to a slot. In particular, removing availability neither destroys an independently Area/GIT-owned creature nor implicitly finalizes a detached materialization. Destruction requires an explicit kill/finalization operation.

Tag is an authored lookup/association value used by explicit retail-compatible routines where their contract calls for it. It is not generic roster identity. Therefore all of these may legally coexist:

- an available NPC record tagged `remote`;
- an available K2 puppet record tagged `remote`;
- an ordinary GIT creature tagged `remote`.

GIT state is a module graph; a detached roster snapshot is a save-wide Party record. Neither representation is opportunistically merged into the other. An explicit Party operation may bind a module creature to a slot, after which roster serialization may snapshot that bound creature into the detached record.

Pointer continuity and runtime ObjectId continuity are not logical continuity.

The canonical PC remains distinct from the currently controlled creature. A controlled companion retains its saved authored Tag; `player` is synthesized only when the source player representation has no meaningful authored Tag. This preserves script lookup behavior without making Tag a roster identity.

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
- live action and delayed-execution objects after the outgoing snapshot has captured any supported Party continuation state;
- Area-bound presentation/reference components of effects.

Durable stats, HP, locals, inventory/equipment, logical roster state, and persistent mechanical effect semantics survive. Effect-derived state is preserved while outgoing Area/presentation bindings retire.

The required source order is:

```text
OnExit -> snapshot durable state -> full Area retirement -> Module destruction
```

For ordinary module travel, supported Party actions and timed commands continue through serialization and reconstruction rather than by retaining outgoing live action objects. Reone captures their save-facing forms while the source graph is authoritative, binds reference-bearing values to exact runtime incarnations, retires the live execution objects with the source Area, and republishes the supported continuation after destination publication. Outgoing-Area-only targets fail closed and cannot capture a destination object through numeric equality. Timed commands retain their original absolute world-time schedule and are not appended twice on repeated travel.

Full disk restoration follows the independently recovered retail `CreateParty` policy: detached NPC/PUP action queues are cleared when those representations are respawned, while saved module/player runtime state follows the saved-world restoration rules. Same-Area control transfer crosses no serialization or reconstruction boundary.

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

`RuntimeObjectRef<T>` therefore stores a weak storage reference plus the bound incarnation. Resolution succeeds only if storage remains, the Object is `Live`, and the incarnation still matches. It never resolves by numeric ID.

Within one runtime-session generation, every numeric runtime ObjectId that has named a published incarnation remains reserved after that incarnation retires. Automatic allocation skips published IDs and explicit same-session reuse is rejected. Full runtime-session retirement clears this published-ID history together with the registry and allocator. This is allocator policy within the runtime-incarnation concept, not another identity domain; `RuntimeObjectRef` still proves exact storage and incarnation rather than relying on numeric nonreuse.

NWScript's object ABI remains numeric. Immediate script arguments and getters may expose the runtime ID of the currently resolved live object. Long-lived internal gameplay references must either retain exact incarnation semantics or be protected by a narrower lifetime plus the session nonreuse rule; a raw numeric value alone never becomes persistence or logical identity.

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
- Every live Item has one stable authoritative disposition: inventory/container ownership, one Creature equipment slot, another supported owned graph, presentation-only clone, or semantic retirement.
- Normal unequip explicitly moves `equipment -> receiver inventory`; cross-owner transfer explicitly detaches the previous equipment/inventory edge before the destination accepts the Item.
- Equipment replacement prevalidates equipability and the displaced receiver, then commits `new Item -> slot` and `old Item -> receiver`. Removing an equipment edge clears the previous owner relation.
- KOTOR shared-inventory selection belongs to action/UI/gameplay policy. Generic `Creature` equipment ownership does not consult Party policy.
- A stack merge finalizes the consumed object immediately.
- Nested children are retired before their owner.
- Presentation clones never take ownership of authoritative gameplay Items.

Fallible GFF/blueprint construction uses object-graph staging. The exact implementation sequence is:

```text
allocate Constructing storage and stage its runtime incarnation/ID
    -> stage authoritative saved identity, when the context owns one
    -> deserialize, validate, and capture required provenance
    -> establish the intended owner relationship while still staged
    -> perform the no-throw owner publication callback
    -> merge prebuilt registry/saved-map nodes
    -> mark the exact candidate incarnations Live
```

Staging is not gameplay publication: candidates are invisible to normal registry lookup until commit. An Area/GIT load uses one outer staged graph, so its nested factories and saved identities do not publish independently. Failure before object-level commit retires the staged candidates, restores the allocation cursor where required, and leaves no semantically live orphan or authoritative saved alias.

Owned-graph replacement discovers the old closure and builds the new closure before a `noexcept` owner swap; publication then merges prebuilt nodes and retires the obsolete graph. Failure before the owner swap leaves the prior graph live. The guarantee addresses ordinary construction/validation failures, not transactional recovery from catastrophic process allocation failure.

`Area::add()` is the Area ownership boundary. A successful call establishes the Area collection, type and Tag indexes, Room/Trigger relationships, and applicable scene/walkmesh attachments. An ordinary recoverable failure removes every relationship installed by that attempt and leaves semantic lifetime with the caller. When the object belongs to an outer staged Area graph it remains `Constructing` until the outer graph commits; adding an already live object changes Area ownership but not its runtime incarnation.

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
- for a template-world save, mandatory `pifo.ifo` player state and `AUTOSAVEPARAMS` timing/waypoint metadata;
- destination L source discovery and mount plan;
- IFO, ARE, GIT, and LYT availability and structural parsing;
- Party slot/member range and duplication constraints;
- authoritative saved graph ObjectId claims and allocator cursor constraints only when `GIT.UseTemplates` declares a saved module graph;
- source module snapshot candidate during an ordinary transition.

### Deliberately post-commit

- authoritative Party mutation and detached-creature materialization;
- runtime registry population and object-level staged graph construction;
- authoritative partial Module ownership, followed by Area/Room and scene construction;
- map, GUI, music, and other presentation changes;
- saved action/effect/event binding and mechanical/query-visible publication;
- authored spawn, `OnLoad`, and `OnEnter` behavior.

C4 makes each object/owned graph correct at this post-commit boundary. The authoritative `_module` may therefore own a partial destination after the C5 commit without making incomplete child objects gameplay-live. A failure there coherently retires the partial destination and lands on the Main Menu; it does not resurrect an old world or claim arbitrary gameplay rollback. Constructing the complete world privately would duplicate the registry, Party, scene, and resource architecture and is deliberately not part of E.

## 11. Actual transition pipelines

### 11.1 Ordinary module transition

```text
prepare destination L plan and validate IFO/ARE/GIT/LYT
    -> source Area OnExit
    -> build frozen source-module snapshot / candidate SaveWorkingState
    -> capture supported Party action/delay continuation against source incarnations
    -> if re-entering the same module, reprepare against that snapshot
    -> prepare loading-screen presentation
    -> COMMIT STARTS
    -> retire source Area runtime and ordinary source runtime objects
    -> retire source saved graph and Module
    -> commit destination ActiveModule/ActiveModuleState resources
    -> adopt the source SaveWorkingState candidate
    -> clear/rebuild post-commit UI and scene state
    -> establish destination identity context
    -> stage/publish the destination Module and structural slot 0 when saved
    -> stage the Area/GIT/nested construction graph, attach ownership, publish live objects
    -> run fresh-module spawn scripts when applicable
    -> establish a default Party when needed
    -> bind saved/detached references and saved runtime state
    -> install captured Party continuation and publish runtime state exactly once
    -> run Module OnLoad
    -> place Party, run fresh member spawn behavior when applicable, and run Area OnEnter
    -> start music and publish Screen::InGame
```

The initial preflight occurs before source `OnExit`. The snapshot is adopted only after module resource publication succeeds. Re-entering the source module is the special case that must reprepare against the newly captured snapshot. Live Party actions never cross the Area boundary: only supported save-facing records and exact already-bound runtime dependencies are reconstructed. A destination restored from a prior module snapshot uses `ModuleGraph`; a destination selected from installed templates uses `Template`.

### 11.2 Saved `ModuleGraph` disk load

```text
exact SaveSlotDescriptor selected by discovery
    -> prepareGameLoad: build unpublished SaveSessionState
    -> decode/validate NFO and GLOBALVARS
    -> decode supplied PartyTable and inventory records
    -> prepare destination L plan and validate IFO/ARE/GIT/LYT
    -> GIT.UseTemplates == 0: validate Party and authoritative ModuleGraph claims
    -> COMMIT: reset/retire old runtime session
    -> commitGameLoad: publish exact save session
    -> commitModuleLoad: publish destination module resources
    -> restore reputation, globals, custom tokens, saved namespace and retail clock
    -> restore Party, PC/controlled representation, and inventory
    -> stage/publish Module slot 0 and the Area/GIT/nested runtime graph
    -> bind saved references and publish query/mechanically visible effects/actions/events
    -> run Module OnLoad
    -> place Party and run Area OnEnter
    -> start music and publish Screen::InGame
```

The exact `SaveSlotDescriptor` is retained from enumeration through preparation and commit. Saved player placement is authoritative for this case. Entry hooks observe restored mechanical state and may query, clear, or replace it; later loader work does not resurrect state removed by a hook.

### 11.3 Template-world disk autosave

Retail transition autosaves may have a complete save session but no archived current-module graph. That is a distinct, valid topology:

```text
prepare unpublished save session and installed destination resources
    -> GIT.UseTemplates == 1: select Template world semantics
    -> decode/validate pifo.ifo Mod_PlayerList and AUTOSAVEPARAMS
    -> validate Party/save-wide state without reserving ModuleGraph IDs
    -> COMMIT: reset old runtime and publish save/module resources
    -> restore reputation, globals, tokens, PartyTable and inventory
    -> restore canonical/current player representation from pifo.ifo as DetachedRecord
    -> seed world time from TIME_PAUSEDAY/TIME_PAUSETIME
    -> construct installed Module/Area/GIT objects with fresh runtime IDs
    -> run fresh world-object spawn scripts
    -> bind/publish supported detached runtime state
    -> run Module OnLoad
    -> place Party at AUTOSAVEPARAMS.STARTWAYPOINT, run member spawn behavior, and run Area OnEnter
    -> start music and publish Screen::InGame
```

`*` denotes no explicit start waypoint. Template object numbers never enter saved-ID reservation or binding. The canonical PC remains distinct when `pifo.ifo` represents a controlled companion, and the saved authored companion Tag is preserved. Full disk `CreateParty` semantics clear detached follower NPC/PUP action queues where retail respawns those representations; this is intentionally different from ordinary module-travel continuation.

### 11.4 Same-Area control transfer

```text
resolve canonical PC or explicit roster slot
    -> establish the explicit Party runtime binding/control state
    -> transfer the outgoing leader transform to the incoming controlled actor
    -> recalculate same-Area placement/camera state
```

There is no snapshot, resource activation, saved-graph retirement, or full Area retirement.

### 11.5 Full runtime-session retirement

```text
mark session non-playable and advance runtime-session generation
    -> stop session gameplay/presentation controllers
    -> release GUI-held runtime references
    -> retire retained Party Area residency while Area owners exist
    -> clear combat and transient Party runtime bindings
    -> clear authoritative scene roots and Module ownership
    -> semantically retire every exact registered runtime incarnation
    -> retire saved graph/generation and clear session-published runtime-ID history
    -> reset runtime allocators
    -> clear save-wide mutable systems in resetGame
    -> retire save/module resource ownership through L
```

Full session retirement is stronger than ordinary module retirement; ordinary travel preserves save-wide and logical Party state.

## 12. Authored-script boundary

The loader is not a transaction over arbitrary NWScript.

| Hook | Boundary |
|---|---|
| Source Area `OnExit` | Runs after initial destination structural preflight but before source snapshot and irreversible commit |
| Fresh-object spawn scripts | Run only for a fresh/template world, after its runtime structure is authoritative |
| Module `OnLoad` | Runs after Module/Area/GIT publication and after saved/detached mechanical runtime state has been bound and published |
| Area `OnEnter` | Runs during Party placement after `OnLoad`, with the same published runtime state visible |

Structural preparation executes no destination-authored script. Once destination scripts begin, the old world is intentionally unrecoverable. A script exception or later post-commit failure causes coherent destination/session retirement and a deliberate Main Menu state.

Publishing saved Effects, Actions, events, and their bound references before entry hooks is semantically required. Shipped scripts query and clear those values. If publication occurred afterward, a hook would inspect an empty object and the loader would then resurrect state the hook intended to remove. Presentation-only effect attachment may follow Area placement where necessary, but mechanically/query-visible effect state does not.

Source `OnExit` is a known weaker boundary: it runs against the authoritative source and arbitrary script mutation is not rollbackable if snapshot creation or same-module repreparation subsequently fails. The design rejects a generic script rollback journal as disproportionate and semantically misleading.

## 13. Failure semantics

| Failure phase | C5 world authority | Object-level result | Terminal behavior |
|---|---|---|---|
| Save cannot open/index or mandatory metadata fails | Current world preserved | Prepared save discarded | Error propagates; current screen/world remains |
| Destination module resources missing or IFO/ARE/GIT/LYT invalid | Current world preserved | Prepared module discarded | Ordinary transition returns failure; disk load rejects before reset |
| Template-autosave `pifo.ifo` or `AUTOSAVEPARAMS` invalid | Current world preserved | Prepared save/module discarded | Disk load rejects before reset |
| Pre-commit Party range/duplication or authoritative saved-ID validation fails | Current world preserved | No candidate runtime object exists | Load rejected before old-session retirement |
| Source `OnExit` throws | Source remains authoritative; prior script side effects may exist | No destination object exists | Ordinary transition returns failure |
| Source snapshot, Party-continuation capture, or same-module repreparation fails | Source remains authoritative; arbitrary `OnExit` mutation is not rolled back | Captured candidates discarded | Ordinary transition returns failure |
| Loading-screen setup fails before commit | Current world preserved | No destination object exists | Previous screen restored |
| Prepared resource plan changes/fails at commit | Old world retirement has started | Best-effort partial retirement | Main Menu with surfaced error |
| Post-commit object deserialization or saved-identity staging fails | Partial destination is authoritative but not playable | Staged object/graph discarded; no live orphan or alias | Partial destination retired; Main Menu |
| Post-commit `Area::add()` ordinary attachment fails | Partial destination is authoritative but not playable | Attempted Area indexes/tenancy/scene edges rolled back | Partial destination retired; Main Menu |
| Saved-runtime binding/publication fails | Old world is unavailable | Unpublished continuation fails closed; partial destination retired | Main Menu |
| Authored spawn/`OnLoad`/`OnEnter` fails | Destination had crossed the authored-gameplay boundary | Arbitrary gameplay is not rolled back | Destination retired; Main Menu |

These are three different scopes: pre-C5 failure preserves the old world; post-C5 failure retires the partial destination; pre-object-commit failure additionally guarantees that the individual candidate never became semantically live. No stable boundary intentionally depends on a later registry or resource cleanup sweep.

## 14. Lifetime hierarchy

| Concept | Identity/lifetime authority | May alias? | Ends when |
|---|---|---:|---|
| Resource source | L `ResourceOwner` plus source identity | Same raw key may exist in several buckets/sources | Owner retires |
| Save session | Exact `SaveSlotDescriptor` and `SaveSessionState` | No active-session alias | Save replacement, new game, or abandonment |
| Saved graph | `SerializedIdentityContext(ModuleGraph)` plus saved-graph generation | Several serialized references may target one live object within the graph | Module saved graph retires |
| Detached record | `SerializedIdentityContext(DetachedRecord)` and record owner | Local numbers may repeat in other records | Record/materialization context ends |
| Logical roster member | `(RosterKind, PartyTable slot)` | Serialized representations may coexist | Logical Party state changes or session ends |
| Runtime incarnation | Runtime ObjectId, exact Object, incarnation, and registry | Numeric ID is not reused within the session; exact incarnation never aliases | Semantic retirement |
| Area residency | Area/Room/Trigger/Pathfinder and scene attachment | One live creature has at most one current residency | Full Area departure |
| Owned nested Item | One inventory/container/equipment ownership edge plus runtime incarnation | A live Item has one stable ownership disposition | Transfer replaces the edge; consumption/owner retirement ends the Item as applicable |
| Presentation | GUI/scene owner and presentation-only object ID | May display copied gameplay data | Presentation closes or detaches |

The session-published runtime-ID history is allocator policy inside the runtime-incarnation row, not an eighth identity domain. It ends with full runtime-session retirement.

## 15. Required invariants and forbidden shortcuts

The implementation must preserve these invariants:

- serialized identity, runtime identity, and roster identity are independent;
- references bind only inside their declared graph/domain lifetime;
- one logical roster slot has at most one explicit live binding;
- logical availability does not confer ownership of, or destroy, a runtime representation;
- one semantically live object has one exact registry entry;
- one numeric runtime ID names at most one published incarnation per runtime session;
- Area-owned state ends before Area owners die;
- supported Party continuation is reconstructed from durable state rather than retaining outgoing live actions;
- nested ownership replacement is all-old or all-new at its owner boundary;
- each live Item has one stable ownership disposition;
- semantic destruction immediately ends runtime resolvability;
- an individual fallible object/Area graph cannot leave a live orphan or saved alias on failed construction;
- pre-commit structural rejection does not replace a good world;
- post-commit failure yields a coherent terminal state;
- K1/K2 differences are policy/data, especially roster and puppet cardinality.

The following are architectural anti-patterns:

- inferring generic roster identity from Tag or ResRef;
- inferring identity from equal saved, detached, or runtime ObjectIds;
- treating a template-world disk restore as an authoritative saved `ModuleGraph`;
- treating roster availability change as semantic Creature destruction;
- retaining Room, Trigger, Pathfinder, combat, or perception state across Area departure;
- retaining raw live action pointers across a module boundary instead of reconstructing supported continuation;
- treating `shared_ptr` storage as gameplay liveness;
- registering a fallible object before deserialization/validation;
- globally publishing candidate objects and relying on cleanup after ordinary construction failure;
- using one ambiguous “unload Party” operation for departure and repositioning;
- using registry membership as persistence identity;
- embedding KOTOR shared-inventory disposition inside generic Creature equipment ownership;
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
- Party binding preserves logical identity without relying on either ID;
- runtime-ID tombstoning strengthens the existing incarnation allocator contract rather than duplicating it;
- object-level staging and C5 preparation solve different scopes: exact-object publication versus avoidable destination rejection.

Normal forward loading remains:

```text
prepare -> construct/validate -> publish -> enter gameplay
```

The design deliberately rejected private duplicates of major subsystems, a second registry, arbitrary rollback journals, and script transactions. Consequently, some post-commit runtime failures go to Main Menu rather than preserving the old world. That is a smaller and more truthful guarantee.

Save/load imposes explicit identity and lifetime contracts on runtime systems, but those contracts also make destruction, GUI previews, object references, inventory transfer, and ordinary module travel safer. None of the identity, roster, residency, liveness, or ownership mechanisms duplicates destination preparation.

The final restoration and hardening work added no lifecycle domain, second registry, shadow Party, parallel scene, or rollback journal, and did not enlarge C5 into a whole-world transaction. Template-autosave metadata handling and detached Party action policy are necessarily KOTOR-facing loader policy, but they remain at the save/module boundary. Generic runtime code does not need to know that a session came from a save. Moving shared-inventory disposition out of `Creature` and into action/UI policy reduced an actual KOTOR-policy leak.

The architecture is modestly larger than pre-E Reone because previously implicit identities and ownership edges are now enforceable. It cannot be materially simplified without collapsing demonstrated independent lifetimes—for example equating save restoration with a saved world, Party availability with object existence, or registry storage with liveness. Normal forward play still follows resource preparation, object construction/publication, Area residency, and authored entry without invoking save-specific reconstruction.

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
- A saved session and an authoritative saved module world are independent; retail template-world autosaves restore save-wide state without inventing a `ModuleGraph`.
- Retail world time is restored and written through `Mod_PauseDay`/`Mod_PauseTime`, with autosave time coming from `AUTOSAVEPARAMS`.
- Saved mechanically/query-visible runtime state is bound and published before destination entry hooks.
- Source working-state mutation is adopted after destination resource publication rather than merely when a snapshot was produced.
- Full Area retirement is not used for same-Area control switching.
- Ordinary travel reconstructs supported Party action/delay continuation after retiring outgoing live execution state.
- Party availability mutation does not own or destroy an independently existing runtime creature.
- Item transfers and equipment replacement now express the displaced ownership receiver explicitly; generic Creature ownership no longer chooses KOTOR Party inventory policy.
- Runtime references no longer assume registry removal destroys all storage.
- Runtime numeric IDs are not reused within one session, and fallible Area/GIT graphs publish through object-level staging.
- The experimental automatic Tag-based GIT/roster reconciliation was rejected; it was not an original E macro principle.
- The final design does not promise a private whole-world transaction. Its bounded preparation guarantee is narrower and simpler.

The evidence supports the conclusion that E's macro lifecycle design remained sound; implementation and correction work supplied identity, roster, residency, liveness, ownership, and publication contracts that the original design left under-specified.

## 19. Remaining Loader roadmap boundaries

- **F:** migrate remaining consumers of the Legacy resource container/backend to the common loader contract.
- **G:** delete the legacy resource architecture after migration and parity validation.
- **H/audit:** optional final enumeration, diagnostics, and cleanup once the cutover is complete.

E must not absorb those backend-migration tasks.

## 20. Deferred observations

The following are intentionally separate from E correctness:

- Area-runtime retirement still enumerates mixed Creature fields. A later incremental `GameObject` durable/runtime/component split can turn that procedural boundary into a smaller owned component without changing E's lifetime law.
- Retail writer parity for `Mod_NextCharId` remains future compatibility work.
- Some weather and transition-related saved GIT fields remain unmodelled.
- Adding a new first-class GIT object type still requires explicit parser/factory/loader registration; metadata-driven registration may reduce that central enumeration later.
- `TemporaryDiscovery` safely restores the current synchronous shared resource view, but true concurrent/background loading requires isolation or synchronization.
- Prepared plans carry selected source identities and decoded core GFFs rather than immutable copies of every later resource. External source mutation between preparation and commit follows the documented terminal policy.
- Source `OnExit` mutation is not rolled back if later snapshot or same-module repreparation fails.
- Actual async execution, replication/network identity, and deeper multiplayer authority remain later architectures.

These are bounded roadmap items, not reasons to enlarge E into a component refactor, immutable content store, or whole-world transaction.
