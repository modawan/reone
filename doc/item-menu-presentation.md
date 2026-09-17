# Inventory and equipment presentation

`InventoryMenu` and `Equipment` use `PresentationGUI` and supplied menu backings.
Their data contracts contain display values, textures and backing-local selection
handles. They do not retain gameplay items or creatures. `PresentationGUI` borrows
an explicit game identity, graphics options, GUI and texture providers, audio
mixer and GUI sounds. These dependencies must outlive the screens. Independent
presentation surfaces need independent GUI providers because loaded GUIs contain
mutable controls and event listeners.

`GameGUI` remains the Game-backed facade for existing screens. Its constructor,
protected gameplay access, common helpers and virtual preload/event hooks remain
available. `InGameMenu` composes the ordinary single-player screens and supplies
its title/footer values and navigation policy to `InGameMenuHost`. The host owns
registrations and their enabled state, and routes input and rendering in the
existing order. It does not select a player, pause gameplay or control the world.

The ordinary single-player factories in `itembacking.h` use the party leader as
the equip subject and the player creature as the shared inventory owner. Names,
descriptions, icons, stack counts and effective subject values come from current
engine getters and description/equipment helpers. These copied views are not an
immutable content database.

Each equipment read invalidates the previous selection handles. A request carries
its read revision, item handle and slot. The backing checks exact runtime
incarnations, current subject and inventory, stack count and equipment freshness;
the gameplay operation also checks live registration and source ownership. A
zero handle explicitly requests clearing. Tags, resource names and list positions
are not item identity. Replacing or releasing a presenter's backing clears its
selection and pending result tracking.

Command delivery and completion are separate interface operations. The ordinary
backing completes synchronously, while the presenter waits for the matching
result before refreshing from authority. Sending a request does not decrement a
count or move an icon optimistically. Inventory's currently disabled use-item
button retains its existing behavior.

`applyEquipmentOperation` delegates the existing candidate evaluation, splitting,
replacement and ownership primitives. It has no hidden party lookup. Rejection
before mutation leaves disposition unchanged. A recoverable failed equip returns
the taken candidate to the supplied inventory; an already cleared paired hand
stays there. Exceptions from core split, transfer or effect operations propagate.
This wrapper does not provide atomic rollback over those operations.

`sharedpresentation.cpp` exercises the production presenters and host with
supplied values and real GUI controls plus a recording renderer, without creating
Game, Party, Creature or Item. `spitempresentation.cpp` exercises the production
Equipment button through its ordinary backing. `itemmenubacking.cpp` covers
instance identity and stale requests, and `equipmentoperation.cpp` covers direct
authoritative operations without a GUI. Pixel comparisons and gameplay smoke
checks remain separate acceptance evidence.
