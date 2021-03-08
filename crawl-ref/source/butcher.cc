/**
 * @file
 * @brief Functions for corpse butchering & devouring.
 **/

#include "AppHdr.h"

#include "butcher.h"

#include "bloodspatter.h"
#include "confirm-butcher-type.h"
#include "command.h"
#include "delay.h"
#include "env.h"
#include "food.h"
#include "god-conduct.h"
#include "item-name.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "libutil.h"
#include "macro.h"
#include "makeitem.h"
#include "message.h"
#include "options.h"
#include "output.h"
#include "rot.h"
#include "stash.h"
#include "stepdown.h"
#include "stringutil.h"

#ifdef TOUCH_UI
#include "invent.h"
#include "menu.h"
#endif

/**
 * Start devouring a corpse.
 *
 * @param corpse       Which corpse to devour.
 * @returns whether the player decided to actually eat the corpse after all.
 */
static bool _start_butchering(item_def& corpse)
{
    // Yes, 0 is correct (no "continue butchering" stage).
    start_delay<ButcherDelay>(0, corpse);

    you.turn_is_over = true;
    return true;
}

void finish_butchering(item_def& corpse)
{
    ASSERT(corpse.base_type == OBJ_CORPSES);
    ASSERT(corpse.sub_type == CORPSE_BODY);

    if (!player_wants_devour_corpse())
        return;

    mprf("You devour %s!", corpse.name(DESC_THE).c_str());

    const int equiv_chunks = 1 + random2(max_corpse_chunks(corpse.mon_type));

    lessen_hunger(CHUNK_BASE_NUTRITION * equiv_chunks, false, HS_ENGORGED);

    if (you.species == SP_GHOUL)
        heal_from_devouring(equiv_chunks);

    butcher_corpse(corpse);
    StashTrack.update_stash(you.pos());
}

static int _corpse_quality(const item_def &item)
{
    return 3 * item.freshness;
}

/**
 * Heal from eating a corpse (ghouls and players in hydra form).
 *
 * @param equiv_chunks The number of chunks that would have been
 *                     obtained by butchering the corpse.
 */
void heal_from_devouring(const int equiv_chunks)
{
    if (!you.duration[DUR_DEATHS_DOOR])
    {
        // cap healing from equivalent chunks at low xls
        const int reduced_chunks = min(equiv_chunks, (1 + you.experience_level) / 2);
        const int healing = 1 + roll_dice(reduced_chunks, 3)
                            + random2avg(1 + you.experience_level / 2, 3);
        canned_msg(MSG_GAIN_HEALTH);
        if (player_rotted() && you.species == SP_GHOUL)
        {
            mpr("You feel more resilient.");
            unrot_hp(equiv_chunks);
        }
        you.heal(healing);
        calc_hp();
        dprf("healed for %d (%d hd)", healing, victim.get_experience_level());
    }
}

/**
 *
 * @param silent Whether this function should print messages.
 * @returns whether the player would benefit from devouring a corpse.
 *
 */
bool player_wants_devour_corpse(bool silent)
{
    if (you.species != SP_GHOUL && you.hunger_state >= HS_ENGORGED)
    {
        if (!silent)
            mprf("You're too full to eat anything.");
        return false;
    }

    // Ghouls with submaximal HP from rot or damage still want to eat
    if (you.species == SP_GHOUL && you.hunger_state >= HS_SATIATED
        && you.hp >= get_real_hp(true, false))
    {
        if (!silent)
            mprf("You would gain no health or satiety from eating a corpse right now.");
        return false;
    }

    return true;
}

/**
 * Attempt to eat a corpse.
 *
 * @param specific_corpse A pointer to the corpse. null means that the player
 *                        chooses what to eat (unless confirm_butcher =
 *                        never).
 * @param only_auto Whether this function was called automatically.
 * @returns whether the player successfully devoured a corpse.
 *
 */
bool devour_corpse(item_def* specific_corpse, bool only_auto)
{
    ASSERT(player_eats_corpses());

    if (you.visible_igrd(you.pos()) == NON_ITEM)
        return false;

    vector<item_def *> all_corpses;

    if (specific_corpse)
        all_corpses.push_back(specific_corpse); // doesn't check position
    else
        for (stack_iterator si(you.pos(), true); si; ++si)
            if (si->is_type(OBJ_CORPSES, CORPSE_BODY))
                all_corpses.push_back(&*si);

    if (all_corpses.empty())
    {
        return false;

    if (!player_wants_devour_corpse(only_auto))
        return false;
    }

    // Non-skeletal corpses
    vector<item_def *> edible_corpses;

    // First determine the edible corpses.
    for (item_def * c : all_corpses)
        if (!is_inedible(*c, false))
            edible_corpses.push_back(c);

    if (edible_corpses.empty())
    {
        if (all_corpses.size() == 1)
        {
            mprf("%s %s.", all_corpses[0]->name(DESC_THE).c_str(),
                "isn't edible");
        }
        else
            mprf("There isn't anything edible to butcher here.");
        return false;
    }

    typedef pair<item_def *, int> corpse_quality;
    vector<corpse_quality> corpse_qualities;

    for (item_def *c : edible_corpses)
        corpse_qualities.emplace_back(c, _corpse_quality(*c));

    stable_sort(begin(corpse_qualities), end(corpse_qualities),
                                            greater_second<corpse_quality>());

    // Eat pre-chosen corpse, if found, or if there is only one corpse.
    if (specific_corpse
        || corpse_qualities.size() == 1
           && Options.confirm_butcher != confirm_butcher_type::always
        || Options.confirm_butcher == confirm_butcher_type::never)
    {
        //XXX: this assumes that we're not being called from a delay ourselves.
        if (_start_butchering(*corpse_qualities[0].first))
            handle_delay();
        return true;
    }

    // Now pick what you want to eat. This is only a problem
    // if there are several corpses on the square.
    bool devoured_any = false;
#ifdef TOUCH_UI
    vector<const item_def*> meat;
    for (const corpse_quality &entry : corpse_qualities)
        meat.push_back(entry.first);

    vector<SelItem> selected =
        select_items(meat, "Choose a corpse to eat", false, menu_type::any);
    redraw_screen();
    for (SelItem sel : selected)
        if (_start_butchering(const_cast<item_def &>(*sel.item)))
            devoured_any = true;
#else
    item_def* to_devour = nullptr;
    bool devour_all = false;
    bool all_done = false;
    for (auto &entry : corpse_qualities)
    {
        item_def * const it = entry.first;
        to_devour = nullptr;

        if (devour_all)
            to_devour = it;
        else
        {
            string corpse_name = it->name(DESC_A);

            bool repeat_prompt = false;
            // Shall we eat this corpse?
            do
            {
                mprf(MSGCH_PROMPT,
                     "Eat %s? [(y)es/(n)o/(a)ll/(q)uit/?]",
                     corpse_name.c_str());
                repeat_prompt = false;

                switch (toalower(getchm(KMC_CONFIRM)))
                {
                case 'a':
                    devour_all = true;
                // fallthrough
                case 'y':
                    to_devour = it;
                    break;

                case 'q':
                CASE_ESCAPE
                    canned_msg(MSG_OK);
                    all_done = true;
                    break;

                case '?':
                    show_butchering_help();
                    clear_messages();
                    redraw_screen();
                    repeat_prompt = true;
                    break;

                default:
                    break;
                }
            }
            while (repeat_prompt && !all_done);
        }

        if (to_devour && _start_butchering(*to_devour))
            devoured_any = true;
        if (all_done)
            break;
    }

    // No point in displaying this if the player pressed 'a' above.
    if (!to_devour && !devour_all && !all_done)
        mprf("There aren't any more corpses to eat here.");
#endif

    //XXX: this assumes that we're not being called from a delay ourselves.
    // It's not a problem in the case of macros, though, because
    // delay.cc:_push_delay should handle them OK.
    if (devoured_any)
        handle_delay();

    return devoured_any;
}

/** Skeletonise this corpse.
 *
 *  @param item the corpse to be turned into a skeleton.
 *  @returns whether a valid skeleton could be made.
 */
bool turn_corpse_into_skeleton(item_def &item)
{
    ASSERT(item.base_type == OBJ_CORPSES);
    ASSERT(item.sub_type == CORPSE_BODY);

    // Some monsters' corpses lack the structure to leave skeletons
    // behind.
    if (!mons_skeleton(item.mon_type))
        return false;

    item.sub_type = CORPSE_SKELETON;
    item.freshness = FRESHEST_CORPSE; // reset rotting counter
    item.rnd = 1 + random2(255); // not sure this is necessary, but...
    item.props.erase(FORCED_ITEM_COLOUR_KEY);
    return true;
}

static void _bleed_monster_corpse(const item_def &corpse)
{
    const coord_def pos = item_pos(corpse);
    if (!pos.origin())
    {
        const int max_chunks = max_corpse_chunks(corpse.mon_type);
        bleed_onto_floor(pos, corpse.mon_type, max_chunks, true);
    }
}

void butcher_corpse(item_def &item, bool skeleton)
{
    item_was_destroyed(item);
    if (!mons_skeleton(item.mon_type))
        skeleton = false;
    if (skeleton)
    {
        _bleed_monster_corpse(item);
        turn_corpse_into_skeleton(item);
    }
    else
    {
        _bleed_monster_corpse(item);
        destroy_item(item.index());
    }
}
