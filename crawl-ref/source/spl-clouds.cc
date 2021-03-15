/**
 * @file
 * @brief Cloud creating spells.
**/

#include "AppHdr.h"

#include "spl-clouds.h"

#include <algorithm>

#include "butcher.h"
#include "cloud.h"
#include "coord.h"
#include "coordit.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "fprop.h"
#include "fight.h"
#include "items.h"
#include "level-state-type.h"
#include "message.h"
#include "mon-behv.h" // ME_WHACK
#include "ouch.h"
#include "random-pick.h"
#include "shout.h"
#include "spl-util.h"
#include "target.h"
#include "terrain.h"

spret conjure_flame(const actor *agent, int pow, const coord_def& where,
                         bool fail)
{
    // FIXME: This would be better handled by a flag to enforce max range.
    if (grid_distance(where, agent->pos()) > spell_range(SPELL_CONJURE_FLAME, pow)
        || !in_bounds(where))
    {
        if (agent->is_player())
            mpr("That's too far away.");
        return spret::abort;
    }

    if (cell_is_solid(where))
    {
        if (agent->is_player())
        {
            const char *feat = feat_type_name(grd(where));
            mprf("You can't place the cloud on %s.", article_a(feat).c_str());
        }
        return spret::abort;
    }

    cloud_struct* cloud = cloud_at(where);

    if (cloud && cloud->type != CLOUD_FIRE)
    {
        if (agent->is_player())
            mpr("There's already a cloud there!");
        return spret::abort;
    }

    actor* victim = actor_at(where);
    if (victim)
    {
        if (agent->can_see(*victim))
        {
            if (agent->is_player())
                mpr("You can't place the cloud on a creature.");
            return spret::abort;
        }

        fail_check();

        // FIXME: maybe should do _paranoid_option_disable() here?
        if (agent->is_player())
            canned_msg(MSG_GHOSTLY_OUTLINE);
        return spret::success;      // Don't give free detection!
    }

    fail_check();

    if (cloud)
    {
        // Reinforce the cloud - but not too much.
        // It must be a fire cloud from a previous test.
        if (you.see_cell(where))
            mpr("The fire blazes with new energy!");
        const int extra_dur = 2 + min(random2(pow) / 2, 20);
        cloud->decay += extra_dur * 5;
        cloud->source = agent->mid;
        if (agent->is_player())
            cloud->set_whose(KC_YOU);
        else
            cloud->set_killer(KILL_MON_MISSILE);
    }
    else
    {
        const int durat = min(5 + (random2(pow)/2) + (random2(pow)/2), 23);
        place_cloud(CLOUD_FIRE, where, durat, agent);
        if (you.see_cell(where))
        {
            if (agent->is_player())
                mpr("The fire ignites!");
            else
                mpr("A cloud of flames bursts into life!");
        }
    }
    noisy(spell_effect_noise(SPELL_CONJURE_FLAME), where);

    return spret::success;
}

spret cast_poisonous_vapours(int pow, const dist &beam, bool fail)
{
    if (cell_is_solid(beam.target))
    {
        canned_msg(MSG_UNTHINKING_ACT);
        return spret::abort;
    }

    monster* mons = monster_at(beam.target);
    if (!mons || mons->submerged())
    {
        fail_check();
        canned_msg(MSG_SPELL_FIZZLES);
        return spret::success; // still losing a turn
    }

    if (actor_cloud_immune(*mons, CLOUD_POISON) && mons->observable())
    {
        mprf("But poisonous vapours would do no harm to %s!",
             mons->name(DESC_THE).c_str());
        return spret::abort;
    }

    if (stop_attack_prompt(mons, false, you.pos()))
        return spret::abort;

    cloud_struct* cloud = cloud_at(beam.target);
    if (cloud && cloud->type != CLOUD_POISON)
    {
        // XXX: consider replacing the cloud instead?
        mpr("There's already a cloud there!");
        return spret::abort;
    }

    fail_check();

    const int cloud_duration = max(random2(pow + 1) / 10, 1); // in dekaauts
    if (cloud)
    {
        // Reinforce the cloud.
        mpr("The poisonous vapours increase!");
        cloud->decay += cloud_duration * 10; // in this case, we're using auts
        cloud->set_whose(KC_YOU);
    }
    else
    {
        place_cloud(CLOUD_POISON, beam.target, cloud_duration, &you);
        mprf("Poisonous vapours surround %s!", mons->name(DESC_THE).c_str());
    }

    behaviour_event(mons, ME_WHACK, &you);

    return spret::success;
}

spret cast_big_c(int pow, spell_type spl, const actor *caster, bolt &beam,
                      bool fail)
{
    if (grid_distance(beam.target, you.pos()) > beam.range
        || !in_bounds(beam.target))
    {
        mpr("That is beyond the maximum range.");
        return spret::abort;
    }

    if (cell_is_solid(beam.target))
    {
        const char *feat = feat_type_name(grd(beam.target));
        mprf("You can't place clouds on %s.", article_a(feat).c_str());
        return spret::abort;
    }

    cloud_type cty = CLOUD_NONE;
    //XXX: there should be a better way to specify beam cloud types
    switch (spl)
    {
        case SPELL_POISONOUS_CLOUD:
            beam.flavour = BEAM_POISON;
            beam.name = "blast of poison";
            cty = CLOUD_POISON;
            break;
        case SPELL_HOLY_BREATH:
            beam.flavour = BEAM_HOLY;
            beam.origin_spell = SPELL_HOLY_BREATH;
            cty = CLOUD_HOLY;
            break;
        case SPELL_FREEZING_CLOUD:
            beam.flavour = BEAM_COLD;
            beam.name = "freezing blast";
            cty = CLOUD_COLD;
            break;
        default:
            mpr("That kind of cloud doesn't exist!");
            return spret::abort;
    }

    beam.thrower           = KILL_YOU;
    beam.hit               = AUTOMATIC_HIT;
    beam.damage            = CONVENIENT_NONZERO_DAMAGE;
    beam.is_tracer         = true;
    beam.use_target_as_pos = true;
    beam.origin_spell      = spl;
    beam.affect_endpoint();
    if (beam.beam_cancelled)
        return spret::abort;

    fail_check();

    big_cloud(cty, caster, beam.target, pow, 8 + random2(3), -1);
    noisy(spell_effect_noise(spl), beam.target);
    return spret::success;
}

/*
 * A cloud_func that places an individual cloud as part of a cloud area. This
 * function is called by apply_area_cloud();
 *
 * @param where       The location of the cloud.
 * @param pow         The spellpower of the spell placing the clouds, which
 *                    determines how long the cloud will last.
 * @param spread_rate How quickly the cloud spreads.
 * @param ctype       The type of cloud to place.
 * @param agent       Any agent that may have caused the cloud. If this is the
 *                    player, god conducts are applied.
 * @param excl_rad    How large of an exclusion radius to make around the
 *                    cloud.
 * @returns           The number of clouds made, which is always 1.
 *
*/
static int _make_a_normal_cloud(coord_def where, int pow, int spread_rate,
                                cloud_type ctype, const actor *agent,
                                int excl_rad)
{
    place_cloud(ctype, where,
                (3 + random2(pow / 4) + random2(pow / 4) + random2(pow / 4)),
                agent, spread_rate, excl_rad);

    return 1;
}

/*
 * Make a large area of clouds centered on a given place. This never creates
 * player exclusions.
 *
 * @param ctype       The type of cloud to place.
 * @param agent       Any agent that may have caused the cloud. If this is the
 *                    player, god conducts are applied.
 * @param where       The location of the cloud.
 * @param pow         The spellpower of the spell placing the clouds, which
 *                    determines how long the cloud will last.
 * @param size        How large a radius of clouds to place from the `where'
 *                    argument.
 * @param spread_rate How quickly the cloud spreads.
*/
void big_cloud(cloud_type cl_type, const actor *agent,
               const coord_def& where, int pow, int size, int spread_rate)
{
    // The starting point _may_ be a place no cloud can be placed on.
    apply_area_cloud(_make_a_normal_cloud, where, pow, size,
                     cl_type, agent, spread_rate, -1);
}

spret cast_ring_of_steam(int power, bool fail)
{
    targeter_radius hitfunc(&you, LOS_NO_TRANS, 1);
    if (stop_attack_prompt(hitfunc, "make a ring of steam",
                [](const actor *act) -> bool {
                    return act->res_fire() < 3;
                }))
    {
        return spret::abort;
    }

    fail_check();
    you.increase_duration(DUR_FIRE_SHIELD,
                          6 + (power / 10) + (random2(power) / 5), 50,
                          "You begin emitting steam!");
    
    manage_fire_shield();
    return spret::success;
}

void manage_fire_shield()
{
    ASSERT(you.duration[DUR_FIRE_SHIELD]);

    // Melt ice armour entirely.
    maybe_melt_player_enchantments(BEAM_FIRE, 100);

    noisy(6, you.pos());

    // Remove steam clouds on top of you
    if (cloud_at(you.pos()) && cloud_at(you.pos())->type == CLOUD_STEAM)
        delete_cloud(you.pos());

    // Place steam clouds all around you
    for (adjacent_iterator ai(you.pos()); ai; ++ai)
        if (!cell_is_solid(*ai) && !cloud_at(*ai))
            place_cloud(CLOUD_STEAM, *ai, 1 + random2(6), &you);
}

spret cast_corpse_rot(bool fail)
{
    fail_check();
    corpse_rot(&you);
    return spret::success;
}

void corpse_rot(actor* caster)
{
    // If there is no caster (god wrath), centre the effect on the player.
    const coord_def center = caster ? caster->pos() : you.pos();
    bool saw_rot = false;
    int did_rot = 0;

    for (radius_iterator ri(center, LOS_NO_TRANS); ri; ++ri)
    {
        if (!is_sanctuary(*ri) && !cloud_at(*ri))
            for (stack_iterator si(*ri); si; ++si)
                if (si->is_type(OBJ_CORPSES, CORPSE_BODY))
                {
                    // Found a corpse. Skeletonise it if possible.
                    if (!mons_skeleton(si->mon_type))
                    {
                        item_was_destroyed(*si);
                        destroy_item(si->index());
                    }
                    else
                        turn_corpse_into_skeleton(*si);

                    if (!saw_rot && you.see_cell(*ri))
                        saw_rot = true;

                    ++did_rot;

                    // Don't look for more corpses here.
                    break;
                }
    }

    for (fair_adjacent_iterator ai(center); ai; ++ai)
    {
        if (did_rot == 0)
            break;

        if (cell_is_solid(*ai))
            continue;

        place_cloud(CLOUD_MIASMA, *ai, 2+random2avg(8, 2),caster);
        --did_rot;
    }

    if (saw_rot)
        mprf("You %s decay.", you.can_smell() ? "smell" : "sense");
    else
        canned_msg(MSG_NOTHING_HAPPENS);
}

void holy_flames(monster* caster, actor* defender)
{
    const coord_def pos = defender->pos();
    int cloud_count = 0;
    const int dur = 8 + random2avg(caster->get_hit_dice() * 3, 2);

    for (adjacent_iterator ai(pos); ai; ++ai)
    {
        if (!in_bounds(*ai)
            || cloud_at(*ai)
            || cell_is_solid(*ai)
            || is_sanctuary(*ai)
            || monster_at(*ai))
        {
            continue;
        }

        place_cloud(CLOUD_HOLY, *ai, dur, caster);

        cloud_count++;
    }

    if (cloud_count)
    {
        if (defender->is_player())
            mpr("Blessed fire suddenly surrounds you!");
        else
            simple_monster_message(*defender->as_monster(),
                                   " is surrounded by blessed fire!");
    }
}
