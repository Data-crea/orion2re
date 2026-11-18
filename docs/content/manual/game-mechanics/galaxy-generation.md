# Manual: Galaxy generation

## Home systems

Every home system is generated with at least three planets. The homeworld and other protected home-system objects are retained if the map must reduce its total number of satellites.

For each non-Lithovore player, galaxy generation ensures that at least one actual food-producing planet is available within six parsecs of the home system. Gas giants and asteroid belts do not satisfy this requirement, and the qualifying system is not assigned a space monster.

Galaxy generation also ensures a nearby star with at least one colonizable planet. If a generated system must be replaced to meet this requirement, it is regenerated using the active map tables rather than receiving fixed star and planet properties.

## Satellite limit

Each system can contain up to five satellites, regardless of galaxy size. The `map satellite_count` table controls initial satellite generation, not the later galaxy-wide trimming pass.

On galaxies with up to 72 total stars, `map planet_limit` sets the combined planet, asteroid-belt, and gas-giant trimming threshold (default: 250). Matching MOO2 1.50, all three satellite types count and are eligible for removal. Reaching the threshold triggers removal until the count falls below it or no removable objects remain. Entire home systems, Orion's system, and systems containing required added food worlds are protected, and planet/system references are updated after removal.

Set `map planet_limit = 0;` to disable this trimming pass, allowing galaxies with up to 72 stars to retain more than 250 satellites when enough are generated. A higher threshold, such as `map planet_limit = 400;`, also permits all five orbital slots across a galaxy of up to 72 stars. This setting does not itself generate more satellites or change their types; use the satellite-count and satellite-type tables for that.

Galaxies with more than 72 total stars use their larger planet capacity instead, regardless of this setting or the galaxy-size preset name.

## Nebulae and black holes

A system's nebula status follows the visible nebula mask. Small dark pockets in the artwork can therefore count as being outside the nebula.

Black holes are not retained inside nebulae. A conflicting black hole is replaced with a normal star generated from the active map tables.

## Special links

Generated wormholes do not connect to Orion.

Galaxy tables, distances, map sizes, home-system requirements, and special-object placement can be changed by mods. See the [galaxy and events configuration reference](../../generated/configuration/galaxy-and-events.md).