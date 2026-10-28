// Atlas layout of the farthest-depth pyramid. Mirrors HizLayoutFor in ClusterCullMath.h.
// Level 0 sits at the origin; levels 1.. are stacked in the column right of it.

uvec2 HizLevelSize(uvec2 base, uint level)
{
    return max(uvec2(1u), (base + ((1u << level) - 1u)) >> level);
}

uvec2 HizLevelOrigin(uvec2 base, uint level)
{
    if (level == 0u) {
        return uvec2(0u);
    }
    uint y = 0u;
    for (uint k = 1u; k < level; ++k) {
        y += HizLevelSize(base, k).y;
    }
    return uvec2(base.x, y);
}
