/**
 * @file
 * @brief Functions for eating.
**/

#include "AppHdr.h"

#include "food.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "butcher.h"
#include "chardump.h"
#include "database.h"
#include "delay.h"
#include "env.h"
#include "god-abil.h"
#include "hints.h"
#include "invent.h"
#include "item-prop.h"
#include "items.h"
#include "item-use.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "misc.h"
#include "mutation.h"
#include "nearby-danger.h"
#include "notes.h"
#include "options.h"
#include "religion.h"
#include "rot.h"
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "travel.h"
#include "transform.h"
#include "xom.h"

static void _describe_food_change(int hunger_increment);

void make_hungry(int hunger_amount, bool suppress_msg,
                 bool magic)
{
    if (crawl_state.disables[DIS_HUNGER])
        return;

    if (you_foodless())
        return;

    if (magic)
        hunger_amount = calc_hunger(hunger_amount);

    if (hunger_amount == 0 && !suppress_msg)
        return;

    you.hunger -= hunger_amount;

    if (you.hunger < 0)
        you.hunger = 0;

    // So we don't get two messages, ever.
    bool state_message = food_change();

    if (!suppress_msg && !state_message)
        _describe_food_change(-hunger_amount);
}

/**
 * Attempt to reduce the player's hunger.
 *
 * @param satiated_amount       The amount by which to reduce hunger by.
 * @param suppress_msg          Whether to squelch messages about hunger
 *                              decreasing.
 * @param max                   The maximum hunger state which the player may
 *                              reach. If -1, defaults to HUNGER_MAXIMUM.
 */
void lessen_hunger(int satiated_amount, bool suppress_msg, int max)
{
    if (you_foodless())
        return;

    you.hunger += satiated_amount;

    const hunger_state_t max_hunger_state = max == -1 ? HS_ENGORGED
                                                      : (hunger_state_t) max;
    ASSERT_RANGE(max_hunger_state, 0, HS_ENGORGED + 1);
    const int max_hunger = min(HUNGER_MAXIMUM,
                               hunger_threshold[max_hunger_state]);
    if (you.hunger > max_hunger)
        you.hunger = max_hunger;

    // So we don't get two messages, ever.
    const bool state_message = food_change();

    if (!suppress_msg && !state_message)
        _describe_food_change(satiated_amount);
}

void set_hunger(int new_hunger_level, bool suppress_msg)
{
    if (you_foodless())
        return;

    int hunger_difference = (new_hunger_level - you.hunger);

    if (hunger_difference < 0)
        make_hungry(-hunger_difference, suppress_msg);
    else if (hunger_difference > 0)
        lessen_hunger(hunger_difference, suppress_msg);
}

bool you_foodless(bool temp)
{
    return you.undead_state(temp) == US_UNDEAD
        || you.undead_state(temp) == US_SEMI_UNDEAD;
}

static bool _eat_check(bool check_hunger = true, bool silent = false,
                                                            bool temp = true)
{
    if (you_foodless(temp))
    {
        if (!silent)
        {
            mpr("You can't eat.");
            crawl_state.zero_turns_taken();
        }
        return false;
    }

    if (!check_hunger)
        return true;

    if (you.hunger_state >= HS_ENGORGED)
    {
        if (!silent)
        {
            mprf("You're too full to eat anything.");
            crawl_state.zero_turns_taken();
        }
        return false;
    }
    return true;
}

static bool _prompt_eat_item(item_def &item)
{
    if (!can_eat(item, false))
        return false;

    if (you.species == SP_GHOUL && you.hunger_state >= HS_SATIATED)
    {
        mprf("You would gain no satiety by eating a ration right now.");
        return false;
    }

    string item_name = item.name(DESC_A);
    bool repeat_prompt = false;
    // Shall we eat this item?
    do
    {
        mprf(MSGCH_PROMPT,
             "Eat %s? [(y)es/(n)o]",
             item_name.c_str());
        repeat_prompt = false;

        switch (toalower(getchm(KMC_CONFIRM)))
        {
        case 'y':
            return eat_item(item);

        default:
            canned_msg(MSG_OK);
            break;
        }
    } while (repeat_prompt);

    return false;
}

// [ds] Returns true if something was eaten.
bool eat_food(bool only_auto)
{
    if (!_eat_check(true, only_auto, true))
        return false;

    bool want_ration = true;

    if (!only_auto && player_eats_corpses())
        want_ration = !devour_corpse(nullptr, only_auto);

    if (only_auto && you.hunger_state >= HS_SATIATED)
        want_ration = false;

    if (want_ration)
    {
        vector<item_def *> snacks;
        bool found_valid = false;

        for (stack_iterator si(you.pos(), true); si; ++si)
        {
            if (si->base_type != OBJ_FOOD)
                continue;

            found_valid = true;
            snacks.push_back(&(*si));
        }

        // Then search through the inventory.
        for (auto &item : you.inv)
        {
            if (!item.defined())
                continue;

            if (item.base_type != OBJ_FOOD)
                continue;

            found_valid = true;
            snacks.push_back(&item);
        }

        if (found_valid)
        {
            if (!only_auto && player_eats_corpses())
                return _prompt_eat_item(*snacks.front());
            else
                return eat_item(*snacks.front());
        }
        else
            mprf(MSGCH_PROMPT, "%s",
                 no_selectables_message(OBJ_FOOD).c_str());
    }

    return false;
}

static string _how_hungry()
{
    if (you.hunger_state > HS_SATIATED)
        return "full";
    return "hungry";
}

hunger_state_t calc_hunger_state()
{
    // Get new hunger state.
    hunger_state_t newstate = HS_FAINTING;
    while (newstate < HS_ENGORGED && you.hunger > hunger_threshold[newstate])
        newstate = (hunger_state_t)(newstate + 1);
    return newstate;
}

// "initial" is true when setting the player's initial hunger state on game
// start or load: in that case it's not really a change, so we suppress the
// state change message and don't identify rings or stimulate Xom.
bool food_change(bool initial)
{
    bool state_changed = false;
    bool less_hungry   = false;

    you.hunger = max(you_min_hunger(), you.hunger);
    you.hunger = min(you_max_hunger(), you.hunger);

    hunger_state_t newstate = calc_hunger_state();

    if (newstate != you.hunger_state)
    {
        state_changed = true;
        if (newstate > you.hunger_state)
            less_hungry = true;

        you.hunger_state = newstate;
        you.redraw_status_lights = true;

        if (newstate < HS_SATIATED)
            interrupt_activity(activity_interrupt::hungry);

        if (!initial)
        {
            string msg = "You ";
            switch (you.hunger_state)
            {
            case HS_FAINTING:
                msg += "are fainting from starvation!";
                mprf(MSGCH_FOOD, less_hungry, "%s", msg.c_str());
                break;

            case HS_STARVING:
                msg += "are starving!";

                mprf(MSGCH_FOOD, less_hungry, "%s", msg.c_str());

                learned_something_new(HINT_YOU_STARVING);
                you.check_awaken(500);
                break;

            case HS_NEAR_STARVING:
                msg += "are near starving!";

                mprf(MSGCH_FOOD, less_hungry, "%s", msg.c_str());

                learned_something_new(HINT_YOU_HUNGRY);
                break;

            case HS_VERY_HUNGRY:
            case HS_HUNGRY:
                msg += "are feeling ";
                if (you.hunger_state == HS_VERY_HUNGRY)
                    msg += "very ";
                msg += _how_hungry();
                msg += ".";

                mprf(MSGCH_FOOD, less_hungry, "%s", msg.c_str());

                learned_something_new(HINT_YOU_HUNGRY);
                break;

            default:
                return state_changed;
            }
        }
    }

    return state_changed;
}

// food_increment is positive for eating, negative for hungering
static void _describe_food_change(int food_increment)
{
    const int magnitude = abs(food_increment);
    string msg;

    if (magnitude == 0)
        return;

    msg = "You feel ";

    if (magnitude <= 100)
        msg += "slightly ";
    else if (magnitude <= 350)
        msg += "somewhat ";
    else if (magnitude <= 800)
        msg += "quite a bit ";
    else
        msg += "a lot ";

    if ((you.hunger_state > HS_SATIATED) ^ (food_increment < 0))
        msg += "more ";
    else
        msg += "less ";

    msg += _how_hungry().c_str();
    msg += ".";
    mpr(msg);
}

// Handle messaging at the end of eating.
// Some food types may not get a message.
static void _finished_eating_message(food_type type)
{
    const bool herbivorous = you.get_mutation_level(MUT_HERBIVOROUS) > 0;

    if (type == FOOD_RATION)
    {
        mpr("That ration really hit the spot!");
        return;
    }
    else if (herbivorous && food_is_meaty(type))
    {
        mpr("Blech - you need greens!");
        return;
    }
}

bool eat_item(item_def &food)
{
    if (food.base_type != OBJ_FOOD)
        return false;

    mprf("You eat %s%s.", food.quantity > 1 ? "one of " : "",
                          food.name(DESC_THE).c_str());

    int value = food_value(food);
    ASSERT(value > 0);
    lessen_hunger(value, true);
    _finished_eating_message(static_cast<food_type>(food.sub_type));

    count_action(CACT_EAT, food.sub_type);

    if (in_inventory(food))
        dec_inv_item_quantity(food.link, 1);
    else
        dec_mitm_item_quantity(food.index(), 1);

    you.turn_is_over = true;
    return true;
}

// Returns true if an item of basetype FOOD or CORPSES cannot currently
// be eaten (respecting species and mutations set).
bool is_inedible(const item_def &item, bool temp)
{
    // Mummies and liches don't eat.
    if (you_foodless(temp))
        return true;

    if (!can_eat(item, true, false, temp))
        return true;

    return false;
}

/** Can the player eat this item?
 *
 *  @param food the item (must be a corpse or food item)
 *  @param suppress_msg whether to print why you can't eat it
 *  @param check_hunger whether to check how hungry you are currently
 *  @param temp whether to factor in temporary forms
 */
bool can_eat(const item_def &food, bool suppress_msg, bool check_hunger,
                                                                bool temp)
{
#define FAIL(msg) { if (!suppress_msg) mpr(msg); return false; }
    if (food.base_type != OBJ_FOOD && food.base_type != OBJ_CORPSES)
        FAIL("That's not food!");

    // [ds] These redundant checks are now necessary - Lua might be calling us.
    if (!_eat_check(check_hunger, suppress_msg, temp))
        return false;

    if (food.base_type == OBJ_CORPSES)
    {
        if (player_eats_corpses() && food.sub_type != CORPSE_SKELETON)
            return true;
        else
        {
            FAIL("Sorry, you don't eat raw flesh.");
            return false;
        }
    }
    // Any food types not specifically handled until here (e.g. meat
    // rations for non-herbivores) are okay.
    return true;
}

int you_max_hunger()
{
    if (you_foodless())
        return HUNGER_DEFAULT;

    // Ghouls can never be full or above.
    if (you.species == SP_GHOUL)
        return hunger_threshold[HS_SATIATED];

    return hunger_threshold[HS_ENGORGED];
}

int you_min_hunger()
{
    // This case shouldn't actually happen.
    if (you_foodless())
        return HUNGER_DEFAULT;

    // Vampires can never starve to death. Ghouls will just rot much faster.
    if (you.undead_state() != US_ALIVE)
        return (HUNGER_FAINTING + HUNGER_STARVING) / 2; // midpoint

    return 0;
}

// General starvation penalties (such as inability to use spells/abilities and
// reduced accuracy) don't apply to bloodless vampires or starving ghouls.
bool apply_starvation_penalties()
{
    return you.hunger_state <= HS_STARVING && !you_min_hunger();
}

static item_def* _get_emergency_food()
{
    // Look for food on floor
    for (stack_iterator si(you.pos(), true); si; ++si)
    {
        if (can_eat(*si, true))
            return &*si;
    }

    // Look in inventory
    auto it = find_if(begin(you.inv), end(you.inv),
                      [](const item_def& inv_item) -> bool
                          {
                              return can_eat(inv_item, true);
                          });
    if (it != end(you.inv))
        return &*it;

    return nullptr;
}

void handle_starvation()
{
    // Don't faint or die while eating.
    if (current_delay() && current_delay()->is_being_used(nullptr, OPER_EAT))
        return;

    if (!you_foodless() && you.hunger <= HUNGER_FAINTING)
    {
        if (!you.cannot_act() && one_chance_in(40))
        {
            mprf(MSGCH_FOOD, "You lose consciousness!");
            stop_running();

            int turns = 5 + random2(8);
            if (!you.duration[DUR_PARALYSIS])
                take_note(Note(NOTE_PARALYSIS, min(turns, 13), 0, "fainting"));
            you.increase_duration(DUR_PARALYSIS, turns, 13);
            xom_is_stimulated(get_tension() > 0 ? 200 : 100);
        }

        if (you.hunger <= 0 && !you.duration[DUR_DEATHS_DOOR])
        {
            if (item_def* emergency_food = _get_emergency_food())
            {
                mpr("As you are about to starve, you manage to eat something.");
                eat_item(*emergency_food);
                return;
            }
            mprf(MSGCH_FOOD, "You have starved to death.");
            ouch(INSTANT_DEATH, KILLED_BY_STARVATION);
            if (!you.pending_revival) // if we're still here...
                set_hunger(HUNGER_DEFAULT, true);
        }
    }
}

static const int hunger_breakpoints[] = { 1, 21, 61, 121, 201, 301, 421 };

int hunger_bars(const int hunger)
{
    return breakpoint_rank(hunger, hunger_breakpoints,
                           ARRAYSZ(hunger_breakpoints));
}

string hunger_cost_string(const int hunger)
{
    if (you_foodless())
        return "N/A";

#ifdef WIZARD
    if (you.wizard)
        return to_string(hunger);
#endif

    const int numbars = hunger_bars(hunger);

    if (numbars > 0)
    {
        return string(numbars, '#')
               + string(ARRAYSZ(hunger_breakpoints) - numbars, '.');
    }
    else
        return "None";
}
