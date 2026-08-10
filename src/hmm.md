## The bug

When a collision stops you, `hit_boundary` snaps your leading edge exactly onto a tile-boundary value:

- Moving **right/down** (`step > 0`): snaps to `offset + i*scale` — the boundary is exactly the **start** of the solid tile in local-space (`local == i`).
- Moving **left/up** (`step < 0`): snaps to `offset + (i+1)*scale` — the boundary is exactly the **start of the tile after** the solid one (`local == i+1`).

Next frame, `i_before = floor(local_before)`. Tiles are treated as half-open `[i, i+1)` — so a point sitting exactly at `local == i` is classified as **already being tile `i`**, while a point sitting exactly at `local == i+1` is classified as tile `i+1` (the free tile, not the solid one).

That's the asymmetry:

- After stopping while moving **left/up**, you're resting exactly at `i+1` → `floor` puts you in the *free* tile next to the wall. Fine — next frame's sweep correctly re-detects the solid tile at `i` when you push into it again.
- After stopping while moving **right/down**, you're resting exactly at `i` → `floor` puts you *already inside the solid tile's own index*. The sweep loop only checks tiles from `i_before + step` onward — it never re-checks `i_before` itself. So the very tile you're flush against is silently excluded from the next check, and if you keep pressing that direction, nothing stops you — you just phase through, even though the *previous* frame's stop looked like it worked.

This is exactly the "stopped once, but then slides through" pattern you're seeing.

## The fix

Don't classify a boundary point using plain `floor` regardless of direction — determine tile index in a way that treats a point flush on a boundary as *not yet inside* the tile it's touching, for whichever direction you're approaching from:

```cpp
// Converts a local coordinate to a tile index, direction-aware. floor() alone
// always assigns an exact boundary point to the tile starting there, which is
// correct when arriving from the positive side (direction < 0) but wrong when
// arriving from the negative side (direction > 0) — in that case, sitting
// exactly on the boundary means you're still flush against the tile's outer
// face, not inside it yet.
auto index_for = [](double local, int direction) -> int {
    if (direction > 0)
        return static_cast<int>(std::ceil(local)) - 1;
    return static_cast<int>(std::floor(local));
};
```

And use it in `sweep_axis` in place of the plain `floor` calls for `i_before`/`i_after` (renaming `step` to `direction` for clarity, same meaning):

```cpp
auto sweep_axis = [&](int axis, double edge_before, double edge_after,
                      double range_lo, double range_hi, double& hit_boundary) -> bool {
    for (const auto& level : handler.placed_levels) {
        const auto& cm = handler.collisions[&level.level];

        const double local_before = (axis == 0) ? to_local({edge_before, 0}, cm).x
                                                  : to_local({0, edge_before}, cm).y;
        const double local_after  = (axis == 0) ? to_local({edge_after, 0}, cm).x
                                                  : to_local({0, edge_after}, cm).y;

        if (local_before == local_after) continue;
        const int direction = (local_after > local_before) ? 1 : -1;

        const int i_before = index_for(local_before, direction);
        const int i_after  = index_for(local_after, direction);

        if (i_before == i_after) continue;

        const double local_lo = (axis == 0) ? to_local({0, range_lo}, cm).y : to_local({range_lo, 0}, cm).x;
        const double local_hi = (axis == 0) ? to_local({0, range_hi}, cm).y : to_local({range_hi, 0}, cm).x;

        const int perp_lo = static_cast<int>(std::floor(std::min(local_lo, local_hi)));
        const int perp_hi = static_cast<int>(std::floor(std::max(local_lo, local_hi)));

        for (int i = i_before + direction; ; i += direction) {
            for (int p = perp_lo; p <= perp_hi; ++p) {
                const bool solid = (axis == 0) ? tile_solid(cm, i, p) : tile_solid(cm, p, i);
                if (solid) {
                    if (axis == 0) {
                        hit_boundary = (direction > 0) ? cm.offset.x + i * cm.scale
                                                        : cm.offset.x + (i + 1) * cm.scale;
                    } else {
                        hit_boundary = (direction > 0) ? -cm.offset.y - i * cm.scale
                                                        : -cm.offset.y - (i + 1) * cm.scale;
                    }
                    return true;
                }
            }
            if (i == i_after) break;
        }
    }
    return false;
};
```

For non-boundary values (the common case), `ceil(x)-1 == floor(x)` for any non-integer `x`, so this only changes behavior at the exact resting-boundary case — it won't affect normal mid-tile movement.

Nothing else in `tick_position` needs to change; the `sweep_axis(0, ...)` / `sweep_axis(1, ...)` call sites stay as they were.
