// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "simulationcraft.hpp"
#include "sc_priest.hpp"

namespace priest
{

  namespace actions
  {
    /**
    * Priest action base class
    *
    * This is a template for common code between priest_spell_t, priest_heal_t and priest_absorb_t.
    * The template is instantiated with either spell_t, heal_t or absorb_t as the 'Base' class.
    * Make sure you keep the inheritance hierarchy and use base_t in the derived class, don't skip it and call
    * spell_t/heal_t or absorb_t directly.
    */
    template <typename Base>
    struct priest_action_t : public Base
    {
      bool shadow_damage_increase;
      bool shadow_dot_increase;

    public:
      priest_action_t(const std::string& n, priest_t& p, const spell_data_t* s = spell_data_t::nil())
        : ab(n, &p, s),
        shadow_damage_increase(ab::data().affected_by(p.specs.shadow_priest->effectN(1))),
        shadow_dot_increase(ab::data().affected_by(p.specs.shadow_priest->effectN(2)))
      {
        ab::may_crit = true;
        ab::tick_may_crit = true;
        ab::weapon_multiplier = 0.0;
        if (shadow_damage_increase)
          ab::base_dd_multiplier *= 1.0 + p.specs.shadow_priest->effectN(1).percent();
        if (shadow_dot_increase)
          ab::base_td_multiplier *= 1.0 + p.specs.shadow_priest->effectN(2).percent();
      }

      priest_td_t& get_td(player_t* t)
      {
        return *(priest().get_target_data(t));
      }

      priest_td_t* find_td(player_t* t)
      {
        return priest().find_target_data(t);
      }

      const priest_td_t* find_td(player_t* t) const
      {
        return priest().find_target_data(t);
      }

      bool trigger_shadowy_insight()
      {
        int stack = priest().buffs.shadowy_insight->check();
        if (priest().buffs.shadowy_insight->trigger())
        {
          priest().cooldowns.mind_blast->reset(true);

          if (priest().buffs.shadowy_insight->check() == stack)
          {
            priest().procs.shadowy_insight_overflow->occur();
          }
          else
          {
            priest().procs.shadowy_insight->occur();
          }
          return true;
        }
        return false;
      }

      bool trigger_zeks()
      {
        if (priest().buffs.zeks_exterminatus->trigger())
        {
          // proc doesn't reset the CD :
          // priest().cooldowns.shadow_word_death->reset(true);

          if (priest().buffs.zeks_exterminatus->check())
          {
            priest().procs.legendary_zeks_exterminatus_overflow->occur();
          }
          else
          {
            priest().procs.legendary_zeks_exterminatus->occur();
          }
          return true;
        }
        return false;
      }

      void trigger_anunds()
      {
        int stack = priest().buffs.anunds_last_breath->check();
        priest().buffs.anunds_last_breath->trigger();

        if (priest().buffs.anunds_last_breath->check() == stack)
        {
          priest().procs.legendary_anunds_last_breath_overflow->occur();
        }
        else
        {
          priest().procs.legendary_anunds_last_breath->occur();
        }
      }

      double cost() const override
      {
        double c = ab::cost();

        if (priest().buffs.power_infusion->check() && priest().specialization() != PRIEST_SHADOW)
        {
          c *= 1.0 + priest().buffs.power_infusion->data().effectN(2).percent();
          c = std::floor(c);
        }

        return c;
      }

      void consume_resource() override
      {
        ab::consume_resource();

        if (ab::base_execute_time > timespan_t::zero() && !this->channeled)
          priest().buffs.borrowed_time->expire();
      }

    };

    struct priest_absorb_t : public priest_action_t<absorb_t>
    {
    public:
      priest_absorb_t(const std::string& n, priest_t& player, const spell_data_t* s = spell_data_t::nil())
        : base_t(n, player, s)
      {
        may_crit = true;
        tick_may_crit = false;
        may_miss = false;
      }
    };

    struct priest_heal_t : public priest_action_t<heal_t>
    {
      priest_heal_t(const std::string& n, priest_t& player, const spell_data_t* s = spell_data_t::nil())
        : base_t(n, player, s)
      {
      }

      double action_multiplier() const override
      {
        double am = base_t::action_multiplier();

        am *= 1.0 + priest().buffs.archangel->current_value;

        return am;
      }

      void execute() override
      {
        priest().buffs.archangel->up();  // benefit tracking

        base_t::execute();

        may_crit = true;
      }

      void impact(action_state_t* s) override
      {
        double save_health_percentage = s->target->health_percentage();

        base_t::impact(s);

        if (s->result_amount > 0)
        {
          if (priest().specialization() != PRIEST_SHADOW && priest().talents.twist_of_fate->ok() &&
            (save_health_percentage < priest().talents.twist_of_fate->effectN(1).base_value()))
          {
            priest().buffs.twist_of_fate->trigger();
          }
        }
      }
    };

    struct priest_spell_t : public priest_action_t<spell_t>
    {
      bool is_sphere_of_insanity_spell;
      bool is_mastery_spell;

      priest_spell_t(const std::string& n, priest_t& player, const spell_data_t* s = spell_data_t::nil())
        : base_t(n, player, s), is_sphere_of_insanity_spell(false), is_mastery_spell(false)
      {
        weapon_multiplier = 0.0;
      }

      bool usable_moving() const override
      {
        if (priest().buffs.surrender_to_madness->check())
        {
          return true;
        }

        return spell_t::usable_moving();
      }

      void impact(action_state_t* s) override
      {
        double save_health_percentage = s->target->health_percentage();


        base_t::impact(s);

        if (result_is_hit(s->result))
        {
          if (priest().specialization() == PRIEST_SHADOW && priest().talents.twist_of_fate->ok() &&
            (save_health_percentage < priest().talents.twist_of_fate->effectN(1).base_value()))
          {
            priest().buffs.twist_of_fate->trigger();
          }
        }
      }

      double action_multiplier() const override
      {
        double m = spell_t::action_multiplier();

        if (is_mastery_spell && priest().mastery_spells.madness->ok())
        {
          m *= 1.0 + priest().cache.mastery_value();
        }

        return m;
      }

      void assess_damage(dmg_e type, action_state_t* s) override
      {
        base_t::assess_damage(type, s);

        if (is_sphere_of_insanity_spell && priest().buffs.sphere_of_insanity->up() && s->result_amount > 0)
        {
          double damage = s->result_amount * priest().buffs.sphere_of_insanity->default_value;
          priest().active_spells.sphere_of_insanity->base_dd_min = damage;
          priest().active_spells.sphere_of_insanity->base_dd_max = damage;
          priest().active_spells.sphere_of_insanity->schedule_execute();
        }
        if (aoe == 0 && result_is_hit(s->result) && priest().buffs.vampiric_embrace->up())
          trigger_vampiric_embrace(s);
      }

      /* Based on previous implementation ( pets don't count but get full heal )
      * and http://www.wowhead.com/spell=15286#comments:id=1796701
      * Last checked 2013/05/25
      */
      void trigger_vampiric_embrace(action_state_t* s)
      {
        double amount = s->result_amount;
        amount *= priest().buffs.vampiric_embrace->data().effectN(1).percent();  // FIXME additive or multiplicate?

                                                                               // Get all non-pet, non-sleeping players
        std::vector<player_t*> ally_list;
        range::remove_copy_if(sim->player_no_pet_list.data(), back_inserter(ally_list), player_t::_is_sleeping);

        for (player_t* ally : ally_list)
        {
          ally->resource_gain(RESOURCE_HEALTH, amount, ally->gains.vampiric_embrace);

          for (pet_t* pet : ally->pet_list)
          {
            pet->resource_gain(RESOURCE_HEALTH, amount, pet->gains.vampiric_embrace);
          }
        }
      }
    };

    namespace spells
    {
      struct angelic_feather_t final : public priest_spell_t
      {
        angelic_feather_t(priest_t& p, const std::string& options_str)
          : priest_spell_t("angelic_feather", p, p.find_class_spell("Angelic Feather"))
        {
          parse_options(options_str);
          harmful = may_hit = may_crit = false;
        }

        void impact(action_state_t* s) override
        {
          priest_spell_t::impact(s);

          if (s->target->buffs.angelic_feather)
          {
            s->target->buffs.angelic_feather->trigger();
          }
        }

        bool ready() override
        {
          if (!target->buffs.angelic_feather)
          {
            return false;
          }

          return priest_spell_t::ready();
        }
      };

      struct divine_star_t final : public priest_spell_t
      {
        divine_star_t(priest_t& p, const std::string& options_str)
          : priest_spell_t("divine_star", p, p.talents.divine_star),
          _heal_spell(new divine_star_base_t<priest_heal_t>("divine_star_heal", p, p.find_spell(110745))),
          _dmg_spell(new divine_star_base_t<priest_spell_t>("divine_star_damage", p, p.find_spell(122128)))
        {
          parse_options(options_str);

          dot_duration = base_tick_time = timespan_t::zero();

          add_child(_heal_spell);
          add_child(_dmg_spell);
        }

        void execute() override
        {
          priest_spell_t::execute();

          _heal_spell->execute();
          _dmg_spell->execute();
        }

      private:
        action_t* _heal_spell;
        action_t* _dmg_spell;
      };

      struct halo_t final : public priest_spell_t
      {
        halo_t(priest_t& p, const std::string& options_str)
          : priest_spell_t("halo", p, p.talents.halo),
          _heal_spell(new halo_base_t<priest_heal_t>("halo_heal", p, p.find_spell(120692))),
          _dmg_spell(new halo_base_t<priest_spell_t>("halo_damage", p, p.find_spell(120696)))
        {
          parse_options(options_str);

          add_child(_heal_spell);
          add_child(_dmg_spell);
        }

        void execute() override
        {
          priest_spell_t::execute();

          _heal_spell->execute();
          _dmg_spell->execute();
        }

      private:
        propagate_const<action_t*> _heal_spell;
        propagate_const<action_t*> _dmg_spell;
      };

      struct levitate_t final : public priest_spell_t
      {
        levitate_t(priest_t& p, const std::string& options_str)
          : priest_spell_t("levitate", p, p.find_class_spell("Levitate"))
        {
          parse_options(options_str);
          ignore_false_positive = true;
        }
      };

      struct power_infusion_t final : public priest_spell_t
      {
        power_infusion_t(priest_t& p, const std::string& options_str)
          : priest_spell_t("power_infusion", p, p.talents.power_infusion)
        {
          parse_options(options_str);
          harmful = false;
        }

        void execute() override
        {
          priest_spell_t::execute();
          priest().buffs.power_infusion->trigger();
          if (sim->debug)
          {
            std::string out = std::string(priest().name()) + " used Power Infusion with " +
              std::to_string(priest().buffs.insanity_drain_stacks->value()) + " Insanity drain stacks.";
            priest().sim->out_debug << out;
          }
        }
      };      

      struct blessed_dawnlight_medallion_t : public priest_spell_t
      {
        double insanity;

        blessed_dawnlight_medallion_t(priest_t& p, const special_effect_t&)
          : priest_spell_t("blessing", p, p.find_spell(227727)), insanity(data().effectN(1).percent())
        {
          energize_amount = RESOURCE_NONE;
          background = true;
        }

        void execute() override
        {
          priest_spell_t::execute();
          priest().generate_insanity(data().effectN(1).percent(), priest().gains.insanity_blessing, execute_state->action);
        }
      };

      struct shadow_word_pain_t final : public priest_spell_t
      {
        double insanity_gain;
        bool casted;

        shadow_word_pain_t(priest_t& p, const std::string& options_str, bool _casted = true)
          : priest_spell_t("shadow_word_pain", p, p.find_class_spell("Shadow Word: Pain")),
          insanity_gain(data().effectN(3).resource(RESOURCE_INSANITY))
        {
          parse_options(options_str);
          casted = _casted;
          may_crit = true;
          tick_zero = false;
          is_mastery_spell = true;
          if (!casted)
          {
            base_dd_max = 0.0;
            base_dd_min = 0.0;
          }
          energize_type = ENERGIZE_NONE;  // disable resource generation from spell data

          if (p.artifact.to_the_pain.rank())
          {
            base_multiplier *= 1.0 + p.artifact.to_the_pain.percent();
          }

          if (priest().specs.shadowy_apparitions->ok() && !priest().active_spells.shadowy_apparitions)
          {
            priest().active_spells.shadowy_apparitions = new shadowy_apparition_spell_t(p);
            if (!priest().artifact.unleash_the_shadows.rank())
            {
              // If SW:P is the only action having SA, then we can add it as a child stat.
              add_child(priest().active_spells.shadowy_apparitions);
            }
          }

          if (priest().artifact.sphere_of_insanity.rank() && !priest().active_spells.sphere_of_insanity)
          {
            priest().active_spells.sphere_of_insanity = new sphere_of_insanity_spell_t(p);
            add_child(priest().active_spells.sphere_of_insanity);
          }
        }

        double spell_direct_power_coefficient(const action_state_t* s) const override
        {
          return casted ? priest_spell_t::spell_direct_power_coefficient(s) : 0.0;
        }

        void impact(action_state_t* s) override
        {
          priest_spell_t::impact(s);

          if (casted)
          {
            priest().generate_insanity(insanity_gain, priest().gains.insanity_shadow_word_pain_onhit, s->action);
          }

          if (priest().active_items.zeks_exterminatus)
          {
            trigger_zeks();
          }
          if (priest().artifact.sphere_of_insanity.rank())
          {
            priest().active_spells.sphere_of_insanity->target_cache.is_valid = false;
          }
        }

        void last_tick(dot_t* d) override
        {
          priest_spell_t::last_tick(d);
          if (priest().artifact.sphere_of_insanity.rank())
          {
            priest().active_spells.sphere_of_insanity->target_cache.is_valid = false;
          }
        }

        void tick(dot_t* d) override
        {
          priest_spell_t::tick(d);

          if (priest().active_spells.shadowy_apparitions && (d->state->result_amount > 0))
          {
            if (d->state->result == RESULT_CRIT)
            {
              priest().active_spells.shadowy_apparitions->trigger();
            }
          }


          if (d->state->result_amount > 0)
          {
            if (priest().rppm.shadowy_insight->trigger())
            {
              trigger_shadowy_insight();
            }
          }

          if (priest().active_items.anunds_seared_shackles)
          {
            trigger_anunds();
          }

          if (priest().active_items.zeks_exterminatus)
          {
            trigger_zeks();
          }
        }

        double cost() const override
        {
          double c = priest_spell_t::cost();

          if (priest().specialization() == PRIEST_SHADOW)
            return 0.0;

          return c;
        }

        double action_multiplier() const override
        {
          double m = priest_spell_t::action_multiplier();

          if (priest().artifact.mass_hysteria.rank())
          {
            m *= 1.0 + (priest().buffs.voidform->check() * (priest().artifact.mass_hysteria.percent()));
          }

          return m;
        }
      };

      struct smite_t final : public priest_spell_t
      {
        smite_t(priest_t& p, const std::string& options_str) : priest_spell_t("smite", p, p.find_class_spell("Smite"))
        {
          parse_options(options_str);

          procs_courageous_primal_diamond = false;
        }

        void execute() override
        {
          priest_spell_t::execute();

          priest().buffs.holy_evangelism->trigger();

          priest().buffs.power_overwhelming->trigger();
        }
      };

      /// Priest Pet Summon Base Spell
      struct summon_pet_t : public priest_spell_t
      {
        timespan_t summoning_duration;
        std::string pet_name;
        propagate_const<pet_t*> pet;

      public:
        summon_pet_t(const std::string& n, priest_t& p, const spell_data_t* sd = spell_data_t::nil())
          : priest_spell_t(n, p, sd), summoning_duration(timespan_t::zero()), pet_name(n), pet(nullptr)
        {
          harmful = false;
        }

        bool init_finished() override
        {
          pet = player->find_pet(pet_name);

          return priest_spell_t::init_finished();
        }

        void execute() override
        {
          pet->summon(summoning_duration);

          priest_spell_t::execute();
        }

        bool ready() override
        {
          if (!pet)
          {
            return false;
          }

          return priest_spell_t::ready();
        }
      };

      struct summon_shadowfiend_t final : public summon_pet_t
      {
        summon_shadowfiend_t(priest_t& p, const std::string& options_str)
          : summon_pet_t("shadowfiend", p, p.find_class_spell("Shadowfiend"))
        {
          parse_options(options_str);
          harmful = false;
          summoning_duration = data().duration();
          cooldown = p.cooldowns.shadowfiend;
          cooldown->duration = data().cooldown();
          cooldown->duration += priest().sets->set(PRIEST_SHADOW, T18, B2)->effectN(1).time_value();
          if (priest().artifact.fiending_dark.rank())
          {
            summoning_duration += priest().artifact.fiending_dark.time_value(1);
          }
        }
      };

      struct summon_mindbender_t final : public summon_pet_t
      {
        summon_mindbender_t(priest_t& p, const std::string& options_str)
          : summon_pet_t("mindbender", p, p.find_talent_spell("Mindbender"))
        {
          parse_options(options_str);
          harmful = false;
          summoning_duration = data().duration();
          cooldown = p.cooldowns.mindbender;
          cooldown->duration = data().cooldown();
          cooldown->duration += priest().sets->set(PRIEST_SHADOW, T18, B2)->effectN(2).time_value();
          if (priest().artifact.fiending_dark.rank())
          {
            summoning_duration += (priest().artifact.fiending_dark.time_value(2) / 3.0);
          }
        }
      };      

    }  // namespace spells

    namespace heals
    {
      struct power_word_shield_t final : public priest_absorb_t
      {
        power_word_shield_t(priest_t& p, const std::string& options_str)
          : priest_absorb_t("power_word_shield", p, p.find_class_spell("Power Word: Shield")), ignore_debuff(false)
        {
          add_option(opt_bool("ignore_debuff", ignore_debuff));
          parse_options(options_str);

          spell_power_mod.direct = 5.5;  // hardcoded into tooltip, last checked 2017-03-18
        }

        void impact(action_state_t* s) override
        {
          priest_absorb_t::impact(s);

          priest().buffs.borrowed_time->trigger();

          if (priest().talents.body_and_soul->ok() && s->target->buffs.body_and_soul)
          {
            s->target->buffs.body_and_soul->trigger();
          }
        }
      };

    }  // namespace heals

  }  // namespace actions

  namespace buffs
  {
    /** Custom insanity_drain_stacks buff */
    struct insanity_drain_stacks_t final : public priest_buff_t<buff_t>
    {
      struct stack_increase_event_t final : public player_event_t
      {
        stack_increase_event_t(insanity_drain_stacks_t* s)
          : player_event_t(*s->player, timespan_t::from_seconds(1.0)), ids(s)
        {
        }

        const char* name() const override
        {
          return "insanity_drain_stack_increase";
        }

        void execute() override
        {
          auto priest = debug_cast<priest_t*>(player());

          priest->insanity.drain();

          // If we are currently channeling Void Torrent or Dispersion, we don't gain stacks.
          if (!(priest->buffs.void_torrent->check() || priest->buffs.dispersion->check()))
          {
            priest->buffs.insanity_drain_stacks->increment();
          }
          // Once the number of insanity drain stacks are increased, adjust the end-event to the new value
          priest->insanity.adjust_end_event();

          // Note, the drain() call above may have drained all insanity in very rare cases, in which case voidform is no
          // longer up. Only keep creating stack increase events if is up.
          if (priest->buffs.voidform->check())
          {
            ids->stack_increase = make_event<stack_increase_event_t>(sim(), ids);
          }
        }
      };

      insanity_drain_stacks_t(priest_t& p)
        : base_t(&p, "insanity_drain_stacks"),
        stack_increase(nullptr)

      {
        set_max_stack(1);
        set_chance(1.0);
        set_duration(timespan_t::zero());
        set_default_value(1);
      }

      bool trigger(int stacks, double value, double chance, timespan_t duration) override
      {
        bool r = base_t::trigger(stacks, value, chance, duration);

        assert(stack_increase == nullptr);
        stack_increase = make_event<stack_increase_event_t>(*sim, this);
        return r;
      }

      void expire_override(int expiration_stacks, timespan_t remaining_duration) override
      {
        event_t::cancel(stack_increase);

        base_t::expire_override(expiration_stacks, remaining_duration);
      }

      void bump(int stacks, double /* value */) override
      {
        buff_t::bump(stacks, current_value + 1);
        // current_value = value + 1;
      }

      void reset() override
      {
        base_t::reset();

        event_t::cancel(stack_increase);
      }
    };    

  }  // namespace buffs

  namespace items
  {
    void do_trinket_init(const priest_t* priest, specialization_e spec, const special_effect_t*& ptr,
      const special_effect_t& effect)
    {
      // Ensure we have the spell data. This will prevent the trinket effect from working on live Simulationcraft. Also
      // ensure correct specialization.
      if (!priest->find_spell(effect.spell_id)->ok() || priest->specialization() != spec)
      {
        return;
      }

      // Set pointer, module considers non-null pointer to mean the effect is "enabled"
      ptr = &(effect);
    }

    // Legion Legendaries

    // Shadow
    void anunds_seared_shackles(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.anunds_seared_shackles, effect);
    }

    void mangazas_madness(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.mangazas_madness, effect);
    }

    void mother_shahrazs_seduction(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.mother_shahrazs_seduction, effect);
    }

    void the_twins_painful_touch(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.the_twins_painful_touch, effect);
    }

    void zenkaram_iridis_anadem(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.zenkaram_iridis_anadem, effect);
    }

    void zeks_exterminatus(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.zeks_exterminatus, effect);
    }

    void heart_of_the_void(special_effect_t& effect)
    {
      priest_t* priest = debug_cast<priest_t*>(effect.player);
      assert(priest);
      do_trinket_init(priest, PRIEST_SHADOW, priest->active_items.heart_of_the_void, effect);
    }

    using namespace unique_gear;

    struct sephuzs_secret_enabler_t : public scoped_actor_callback_t<priest_t>
    {
      sephuzs_secret_enabler_t() : scoped_actor_callback_t(PRIEST)
      {
      }

      void manipulate(priest_t* priest, const special_effect_t& e) override
      {
        priest->legendary.sephuzs_secret = e.driver();
      }
    };

    struct sephuzs_secret_t : public class_buff_cb_t<priest_t, haste_buff_t, haste_buff_creator_t>
    {
      sephuzs_secret_t() : super(PRIEST, "sephuzs_secret")
      {
      }

      haste_buff_t*& buff_ptr(const special_effect_t& e) override
      {
        return debug_cast<priest_t*>(e.player)->buffs.sephuzs_secret;
      }

      haste_buff_creator_t creator(const special_effect_t& e) const override
      {
        return super::creator(e)
          .spell(e.trigger())
          .cd(e.player->find_spell(226262)->duration())
          .default_value(e.trigger()->effectN(2).percent())
          .add_invalidate(CACHE_RUN_SPEED);
      }
    };

    void init()
    {
      // Legion Legendaries
      unique_gear::register_special_effect(215209, anunds_seared_shackles);
      unique_gear::register_special_effect(207701, mangazas_madness);
      unique_gear::register_special_effect(236523, mother_shahrazs_seduction);
      unique_gear::register_special_effect(207721, the_twins_painful_touch);
      unique_gear::register_special_effect(224999, zenkaram_iridis_anadem);
      unique_gear::register_special_effect(236545, zeks_exterminatus);
      unique_gear::register_special_effect(208051, sephuzs_secret_enabler_t());
      unique_gear::register_special_effect(208051, sephuzs_secret_t(), true);
      unique_gear::register_special_effect(248296, heart_of_the_void);
    }

  }  // namespace items

     // ==========================================================================
     // Priest Targetdata Definitions
     // ==========================================================================

  priest_td_t::priest_td_t(player_t* target, priest_t& p)
    : actor_target_data_t(target, &p), dots(), buffs(), priest(p)
  {
    dots.holy_fire = target->get_dot("holy_fire", &p);
    dots.power_word_solace = target->get_dot("power_word_solace", &p);
    dots.shadow_word_pain = target->get_dot("shadow_word_pain", &p);
    dots.vampiric_touch = target->get_dot("vampiric_touch", &p);

    buffs.schism = buff_creator_t(*this, "schism", p.talents.schism);

    target->callbacks_on_demise.push_back([this](player_t*) { target_demise(); });
  }

  void priest_td_t::reset()
  {
  }

  void priest_td_t::target_demise()
  {
    if (priest.sim->debug)
    {
      priest.sim->out_debug.printf("Player %s demised. Priest %s resets targetdata for him.", target->name(),
        priest.name());
    }

    reset();
  }

  // ==========================================================================
  // Priest Definitions
  // ==========================================================================

  priest_t::priest_t(sim_t* sim, const std::string& name, race_e r)
    : player_t(sim, PRIEST, name, r),
    buffs(),
    talents(),
    specs(),
    mastery_spells(),
    cooldowns(),
    gains(),
    benefits(),
    procs(),
    rppm(),
    active_spells(),
    active_items(),
    pets(),
    options(),
    insanity(*this)
  {
    create_cooldowns();
    create_gains();
    create_procs();
    create_benefits();

    regen_type = REGEN_DYNAMIC;
  }

  /** Construct priest cooldowns */
  void priest_t::create_cooldowns()
  {
    cooldowns.chakra = get_cooldown("chakra");
    cooldowns.mindbender = get_cooldown("mindbender");
    cooldowns.penance = get_cooldown("penance");
    cooldowns.power_word_shield = get_cooldown("power_word_shield");
    cooldowns.shadowfiend = get_cooldown("shadowfiend");
    cooldowns.silence = get_cooldown("silence");
    cooldowns.mind_blast = get_cooldown("mind_blast");
    // cooldowns.shadow_word_death = get_cooldown( "shadow_word_death" );
    cooldowns.shadow_word_void = get_cooldown("shadow_word_void");
    cooldowns.void_bolt = get_cooldown("void_bolt");
    cooldowns.mind_bomb = get_cooldown("mind_bomb");
    cooldowns.sephuzs_secret = get_cooldown("sephuzs_secret");

    if (specialization() == PRIEST_DISCIPLINE)
    {
      cooldowns.power_word_shield->duration = timespan_t::zero();
    }
    else
    {
      cooldowns.power_word_shield->duration = timespan_t::from_seconds(4.0);
    }
    talent_points.register_validity_fn([this](const spell_data_t* spell) {
      // Soul of the High Priest
      if (find_item(151646))
      {
        return spell->id() == 109142;  // Twist of Fate
      }

      return false;
    });
  }

  /** Construct priest gains */
  void priest_t::create_gains()
  {
    gains.mindbender = get_gain("Mana Gained from Mindbender");
    gains.power_word_solace = get_gain("Mana Gained from Power Word: Solace");
    gains.insanity_auspicious_spirits = get_gain("Insanity Gained from Auspicious Spirits");
    gains.insanity_dispersion = get_gain("Insanity Saved by Dispersion");
    gains.insanity_drain = get_gain("Insanity Drained by Voidform");
    gains.insanity_mind_blast = get_gain("Insanity Gained from Mind Blast");
    gains.insanity_mind_flay = get_gain("Insanity Gained from Mind Flay");
    gains.insanity_mind_sear = get_gain("Insanity Gained from Mind Sear");
    gains.insanity_pet = get_gain("Insanity Gained from Shadowfiend");
    gains.insanity_power_infusion = get_gain("Insanity Gained from Power Infusion");
    gains.insanity_shadow_crash = get_gain("Insanity Gained from Shadow Crash");
    gains.insanity_shadow_word_death = get_gain("Insanity Gained from Shadow Word: Death");
    gains.insanity_shadow_word_pain_onhit = get_gain("Insanity Gained from Shadow Word: Pain Casts");
    gains.insanity_shadow_word_void = get_gain("Insanity Gained from Shadow Word: Void");
    gains.insanity_surrender_to_madness = get_gain("Insanity Gained from Surrender to Madness");
    gains.insanity_vampiric_touch_ondamage = get_gain("Insanity Gained from Vampiric Touch Damage (T19 2P)");
    gains.insanity_vampiric_touch_onhit = get_gain("Insanity Gained from Vampiric Touch Casts");
    gains.insanity_void_bolt = get_gain("Insanity Gained from Void Bolt");
    gains.insanity_void_torrent = get_gain("Insanity Saved by Void Torrent");
    gains.vampiric_touch_health = get_gain("Health from Vampiric Touch Ticks");
    gains.insanity_blessing = get_gain("Insanity from Blessing Dawnlight Medallion");
    gains.insanity_call_to_the_void = get_gain("Insanity Gained from Call to the Void");
  }

  /** Construct priest procs */
  void priest_t::create_procs()
  {
    procs.shadowy_apparition = get_proc("Shadowy Apparition Procced");
    procs.shadowy_apparition = get_proc("Shadowy Apparition Insanity lost to overflow");
    procs.shadowy_insight = get_proc("Shadowy Insight Mind Blast CD Reset from Shadow Word: Pain");
    procs.shadowy_insight_overflow = get_proc("Shadowy Insight Mind Blast CD Reset lost to overflow");
    procs.surge_of_light = get_proc("Surge of Light");
    procs.surge_of_light_overflow = get_proc("Surge of Light lost to overflow");
    procs.serendipity = get_proc("Serendipity (Non-Tier 17 4pc)");
    procs.serendipity_overflow = get_proc("Serendipity lost to overflow (Non-Tier 17 4pc)");

    procs.void_tendril = get_proc("Void Tendril spawned from Call to the Void");

    procs.legendary_anunds_last_breath = get_proc(
      "Legendary - Anund's Seared Shackles - Void Bolt damage increases (3% "
      "per)");
    procs.legendary_anunds_last_breath_overflow = get_proc(
      "Legendary - Anund's Seared Shackles - Void Bolt damage increases (3% "
      "per) lost to overflow");

    procs.legendary_zeks_exterminatus = get_proc(
      "Legendary - Zek's Exterminatus - Shadow Word Death damage increases "
      "(25% "
      "per)");
    procs.legendary_zeks_exterminatus_overflow = get_proc(
      "Legendary - Zek's Exterminatus - Shadow Word Death damage increases "
      "(100% "
      "per) lost to overflow");
  }

  /** Construct priest benefits */
  void priest_t::create_benefits()
  {
  }

  /**
  * Define the acting role of the priest().
  *
  * If base_t::primary_role() has a valid role defined, use it, otherwise select spec-based default.
  */
  role_e priest_t::primary_role() const
  {
    switch (base_t::primary_role())
    {
    case ROLE_HEAL:
      return ROLE_HEAL;
    case ROLE_DPS:
    case ROLE_SPELL:
      return ROLE_SPELL;
    default:
      if (specialization() == PRIEST_HOLY)
        return ROLE_HEAL;
      break;
    }

    return ROLE_SPELL;
  }

  /**
  * @brief Convert hybrid stats
  *
  *  Converts hybrid stats that either morph based on spec or only work
  *  for certain specs into the appropriate "basic" stats
  */
  stat_e priest_t::convert_hybrid_stat(stat_e s) const
  {
    switch (s)
    {
    case STAT_STR_AGI_INT:
    case STAT_STR_INT:
    case STAT_AGI_INT:
      return STAT_INTELLECT;
    case STAT_STR_AGI:
      return STAT_NONE;
    case STAT_SPIRIT:
      if (specialization() != PRIEST_SHADOW)
      {
        return s;
      }
      return STAT_NONE;
    case STAT_BONUS_ARMOR:
      return STAT_NONE;
    default:
      return s;
    }
  }

  expr_t* priest_t::create_expression(action_t* a, const std::string& name_str)
  {
    if (name_str == "primary_target")
    {
      return make_fn_expr(name_str, [this, a]() { return target == a->target; });
    }

    else if (name_str == "shadowy_apparitions_in_flight")
    {
      return make_fn_expr(name_str, [this]() {
        if (!active_spells.shadowy_apparitions)
          return 0.0;

        return static_cast<double>(active_spells.shadowy_apparitions->num_travel_events());
      });
    }

    else if (name_str == "current_insanity_drain")
    {
      // Current Insanity Drain for the next 1.0 sec.
      // Does not account for a new stack occurring in the middle and can be anywhere from 0.0 - 0.5 off the real value.
      return make_fn_expr(name_str, [this]() { return (insanity.insanity_drain_per_second()); });
    }

    return player_t::create_expression(a, name_str);
  }

  void priest_t::assess_damage(school_e school, dmg_e dtype, action_state_t* s)
  {
    if (buffs.shadowform->check())
    {
      s->result_amount *= 1.0 + buffs.shadowform->check() * buffs.shadowform->data().effectN(2).percent();
    }

    player_t::assess_damage(school, dtype, s);
  }

  double priest_t::composite_spell_haste() const
  {
    double h = player_t::composite_spell_haste();

    if (buffs.power_infusion->check())
      h /= 1.0 + buffs.power_infusion->data().effectN(1).percent();

    if (buffs.lingering_insanity->check())
    {
      h /= 1.0 + (buffs.lingering_insanity->check()) *
        buffs.lingering_insanity->data().effectN(1).percent();
    }

    if (buffs.voidform->check())
    {
      h /= 1.0 + (buffs.voidform->check() *
        find_spell(228264)->effectN(2).percent() / 10.0);
    }

    if (buffs.sephuzs_secret->check())
    {
      h /= (1.0 + buffs.sephuzs_secret->stack_value());
    }

    // 7.2 Sephuz's Secret passive haste. If the item is missing, default_chance will be set to 0 (by
    // the fallback buff creator).
    if (legendary.sephuzs_secret->ok())
    {
      h /= (1.0 + legendary.sephuzs_secret->effectN(3).percent());
    }

    return h;
  }

  double priest_t::composite_melee_haste() const
  {
    double h = player_t::composite_melee_haste();

    if (buffs.power_infusion->check())
      h /= 1.0 + buffs.power_infusion->data().effectN(1).percent();

    if (buffs.lingering_insanity->check())
    {
      h /= 1.0 + (buffs.lingering_insanity->check() - 1) *
        buffs.lingering_insanity->data().effectN(1).percent();
    }

    if (buffs.voidform->check())
    {
      h /= 1.0 + (buffs.voidform->check() *
        find_spell(228264)->effectN(2).percent() / 10.0);
    }

    return h;
  }

  double priest_t::composite_spell_speed() const
  {
    double h = player_t::composite_spell_speed();

    if (buffs.borrowed_time->check())
      h /= 1.0 + buffs.borrowed_time->data().effectN(1).percent();

    return h;
  }

  double priest_t::composite_melee_speed() const
  {
    double h = player_t::composite_melee_speed();

    if (buffs.borrowed_time->check())
      h /= 1.0 + buffs.borrowed_time->data().effectN(1).percent();

    return h;
  }

  double priest_t::composite_player_pet_damage_multiplier(const action_state_t* state) const
  {
    double m = player_t::composite_player_pet_damage_multiplier(state);

    m *= 1.0 + artifact.darkening_whispers.percent(2);
    m *= 1.0 + artifact.darkness_of_the_conclave.percent(3);

    return m;
  }

  double priest_t::composite_player_multiplier(school_e school) const
  {
    double m = base_t::composite_player_multiplier(school);

    if (specialization() == PRIEST_SHADOW) // TODO: check if this is really increasing all spells
    {
      if (buffs.shadowform->check())
      {
        m *= 1.0 + buffs.shadowform->data().effectN(1).percent() * buffs.shadowform->check();
      }

      if (specs.voidform->ok() && buffs.voidform->check())
      {
        double voidform_multiplier = buffs.voidform->data().effectN(1).percent();

        if (active_items.zenkaram_iridis_anadem)
        {
          voidform_multiplier += buffs.iridis_empowerment->data().effectN(2).percent();
        }
        m *= 1.0 + voidform_multiplier;
      }

      m *= 1.0 + artifact.creeping_shadows.percent();
      m *= 1.0 + artifact.darkening_whispers.percent();
    }
    m *= 1.0 + artifact.darkness_of_the_conclave.percent();
    m *= 1.0 + buffs.twist_of_fate->current_value;

    return m;
  }

  double priest_t::composite_player_heal_multiplier(const action_state_t* s) const
  {
    double m = player_t::composite_player_heal_multiplier(s);

    if (buffs.twist_of_fate->check())
    {
      m *= 1.0 + buffs.twist_of_fate->current_value;
    }

    if (specs.grace->ok())
      m *= 1.0 + specs.grace->effectN(1).percent();

    return m;
  }

  double priest_t::composite_player_absorb_multiplier(const action_state_t* s) const
  {
    double m = player_t::composite_player_absorb_multiplier(s);

    if (specs.grace->ok())
      m *= 1.0 + specs.grace->effectN(2).percent();

    return m;
  }

  double priest_t::composite_player_target_multiplier(player_t* t, school_e school) const
  {
    double m = player_t::composite_player_target_multiplier(t, school);

    if (const auto td = find_target_data(t))
    {
      if (td->buffs.schism->check())
      {
        m *= 1.0 + td->buffs.schism->data().effectN(2).percent();
      }
    }

    return m;
  }

  double priest_t::matching_gear_multiplier(attribute_e attr) const
  {
    if (attr == ATTR_INTELLECT)
      return 0.05;

    return 0.0;
  }

  action_t* priest_t::create_action(const std::string& name, const std::string& options_str)
  {
    using namespace actions::spells;
    using namespace actions::heals;

    action_t* shadow_action = create_action_shadow( name, options_str );
    if ( shadow_action )
      return shadow_action;

    action_t* discipline_action = create_action_discipline( name, options_str );
    if ( discipline_action )
      return discipline_action;

    action_t* holy_action = create_action_holy( name, options_str );
    if ( holy_action )
      return holy_action;

    return base_t::create_action(name, options_str);
  }

  pet_t* priest_t::create_pet(const std::string& pet_name, const std::string& /* pet_type */)
  {
    // pet_t* p = find_pet( pet_name );

    if (pet_name == "shadowfiend")
      return new pets::fiend::shadowfiend_pet_t(sim, *this);
    if (pet_name == "mindbender")
      return new pets::fiend::mindbender_pet_t(sim, *this);

    sim->errorf("Tried to create priest pet %s.", pet_name.c_str());

    return nullptr;
  }

  void priest_t::create_pets()
  {
    base_t::create_pets();

    if (find_action("shadowfiend") && !talents.mindbender->ok())
    {
      pets.shadowfiend = create_pet("shadowfiend");
    }

    if ((find_action("mindbender") || find_action("shadowfiend")) && talents.mindbender->ok())
    {
      pets.mindbender = create_pet("mindbender");
    }
  }

  void priest_t::init_base_stats()
  {
    base_t::init_base_stats();

    base.attack_power_per_strength = 0.0;
    base.attack_power_per_agility = 0.0;
    base.spell_power_per_intellect = 1.0;

    if (specialization() == PRIEST_SHADOW)
      resources.base[RESOURCE_INSANITY] = 100.0;

    // Discipline/Holy
    base.mana_regen_from_spirit_multiplier = specs.meditation_disc->ok() ? specs.meditation_disc->effectN(1).percent()
      : specs.meditation_holy->effectN(1).percent();
  }

  void priest_t::init_resources(bool force)
  {
    base_t::init_resources(force);

    resources.current[RESOURCE_INSANITY] = 0.0;
  }

  void priest_t::init_scaling()
  {
    base_t::init_scaling();

    // Atonement heals are capped at a percentage of the Priest's health, so there may be scaling with stamina.
    if (specialization() == PRIEST_DISCIPLINE && specs.atonement->ok() && primary_role() == ROLE_HEAL)
      scaling->enable(STAT_STAMINA);

    if( specialization() == PRIEST_SHADOW )
    // Just hook insanity init in here when actor set bonuses are ready
    insanity.init();
  }

  void priest_t::init_spells()
  {
    base_t::init_spells();

    init_spells_shadow();
    init_spells_discipline();
    init_spells_holy();

    // Mastery Spells
    mastery_spells.absolution    = find_mastery_spell(PRIEST_DISCIPLINE);
    mastery_spells.echo_of_light = find_mastery_spell(PRIEST_HOLY);
    mastery_spells.madness       = find_mastery_spell(PRIEST_SHADOW);
  }

  void priest_t::create_buffs()
  {
    base_t::create_buffs();

    create_buffs_shadow();
    create_buffs_discipline();
    create_buffs_holy();

  }

  void priest_t::init_rng()
  {
    init_rng_shadow();
    init_rng_discipline();
    init_rng_holy();

    player_t::init_rng();
  }

  /// ALL Spec Pre-Combat Action Priority List
  void priest_t::apl_precombat()
  {
    action_priority_list_t* precombat = get_action_priority_list("precombat");
    // Snapshot stats
    precombat->add_action("flask");
    precombat->add_action("food");
    precombat->add_action("augmentation");
    precombat->add_action("snapshot_stats",
      "Snapshot raid buffed stats before combat begins and "
      "pre-potting is done.");

    // do all kinds of calculations here to reduce CPU time
    if (specialization() == PRIEST_SHADOW)
    {
      precombat->add_action(
        "variable,name=haste_eval,op=set,value=(raw_haste_pct-0.3)*(10+10*equipped."
        "mangazas_madness+5*talent.fortress_of_the_mind.enabled)");
      precombat->add_action("variable,name=haste_eval,op=max,value=0");
      precombat->add_action(
        "variable,name=erupt_eval,op=set,value=26+1*talent.fortress_of_the_mind.enabled-"
        "3*talent.Shadowy_insight.enabled+variable.haste_eval*1.5");
      precombat->add_action(
        "variable,name=cd_time,op=set,value=(12+(2-2*talent.mindbender.enabled*set_"
        "bonus.tier20_4pc)*set_bonus.tier19_2pc+(1-3*talent.mindbender.enabled*set_"
        "bonus.tier20_4pc)*equipped.mangazas_madness+(6+5*talent.mindbender.enabled)"
        "*set_bonus.tier20_4pc+2*artifact.lash_of_insanity.rank)");
      precombat->add_action(
        "variable,name=dot_swp_dpgcd,op=set,value=36.5*1.2*(1+0.06*artifact.to_the_pain.rank)"
        "*(1+0.2+stat.mastery_rating%16000)*0.75");
      precombat->add_action(
        "variable,name=dot_vt_dpgcd,op=set,value=68*1.2*(1+0.05*artifact.touch_of_"
        "darkness.rank)"
        "*(1+0.2+stat.mastery_rating%16000)*0.5");
      precombat->add_action("variable,name=sear_dpgcd,op=set,value=120*1.2*(1+0.05*artifact.void_corruption.rank)");
      precombat->add_action(
        "variable,name=s2msetup_time,op=set,value=(0.8*(83+(20+20*talent.fortress_of_the_mind"
        ".enabled)*set_bonus.tier20_4pc+((33-13*set_bonus.tier20_4pc)*"
        "talent.reaper_of_souls.enabled)+set_bonus.tier19_2pc*4+8*equipped.mangazas_madness+(raw_haste_"
        "pct*10*(1+0.7*set_bonus.tier20_4pc))*(2+(0.8*set_bonus.tier19_2pc)+(1*talent.reaper_of_souls."
        "enabled)+(2*artifact.mass_hysteria.rank)))),if=talent.surrender_to_madness.enabled");
      precombat->add_action("potion");
    }

    // Precast
    switch (specialization())
    {
    case PRIEST_DISCIPLINE:
      break;
    case PRIEST_HOLY:
      break;
    case PRIEST_SHADOW:
    default:
      precombat->add_action(this, "Shadowform", "if=!buff.shadowform.up");
      precombat->add_action("mind_blast");
      break;
    }
  }

  std::string priest_t::default_potion() const
  {
    std::string lvl110_potion = "prolonged_power";

    return (true_level >= 100)
      ? lvl110_potion
      : (true_level >= 90)
      ? "draenic_intellect"
      : (true_level >= 85) ? "jade_serpent" : (true_level >= 80) ? "volcanic" : "disabled";
  }

  std::string priest_t::default_flask() const
  {
    return (true_level >= 100)
      ? "whispered_pact"
      : (true_level >= 90)
      ? "greater_draenic_intellect_flask"
      : (true_level >= 85) ? "warm_sun" : (true_level >= 80) ? "draconic_mind" : "disabled";
  }

  std::string priest_t::default_food() const
  {
    std::string lvl100_food = "buttered_sturgeon";

    return (true_level > 100)
      ? "azshari_salad"
      : (true_level > 90)
      ? lvl100_food
      : (true_level >= 90) ? "mogu_fish_stew"
      : (true_level >= 80) ? "seafood_magnifique_feast" : "disabled";
  }

  std::string priest_t::default_rune() const
  {
    return (true_level >= 110) ? "defiled" : (true_level >= 100) ? "focus" : "disabled";
  }

  void priest_t::trigger_sephuzs_secret(const action_state_t* state, spell_mechanic mechanic, double proc_chance)
  {
    // Skip the attempt to trigger sephuz if it is suppressed
    if (options.priest_suppress_sephuz)
    {
      return;
    }

    switch (mechanic)
    {
      // Interrupts will always trigger sephuz
    case MECHANIC_INTERRUPT:
      break;
    default:
      // By default, proc sephuz on persistent enemies if they are below the "boss level"
      // (playerlevel + 3), and on any kind of transient adds.
      if (state->target->type != ENEMY_ADD && (state->target->level() >= sim->max_player_level + 3))
      {
        return;
      }
      break;
    }

    // Ensure Sephuz's Secret can even be procced. If the ring is not equipped, a fallback buff with
    // proc chance of 0 (disabled) will be created
    if (buffs.sephuzs_secret->default_chance == 0)
    {
      return;
    }

    buffs.sephuzs_secret->trigger(1, buff_t::DEFAULT_VALUE(), proc_chance);
    cooldowns.sephuzs_secret->start();
  }

  /** NO Spec Combat Action Priority List */
  void priest_t::create_apl_default()
  {
    action_priority_list_t* def = get_action_priority_list("default");

    // DEFAULT
    if (sim->allow_potions)
      def->add_action("mana_potion,if=mana.pct<=75");

    if (find_class_spell("Shadowfiend")->ok())
    {
      def->add_action(this, "Shadowfiend");
    }
    if (race == RACE_TROLL)
      def->add_action("berserking");
    if (race == RACE_BLOOD_ELF)
      def->add_action("arcane_torrent,if=mana.pct<=90");
    def->add_action(this, "Shadow Word: Pain", ",if=remains<tick_time|!ticking");
    def->add_action(this, "Smite");
  }

  /**
  * Obtain target_data for given target.
  * Always returns non-null targetdata pointer
  */
  priest_td_t* priest_t::get_target_data(player_t* target) const
  {
    priest_td_t*& td = _target_data[target];
    if (!td)
    {
      td = new priest_td_t(target, const_cast<priest_t&>(*this));
    }
    return td;
  }

  /**
  * Find target_data for given target.
  * Returns target_data if found, nullptr otherwise
  */
  priest_td_t* priest_t::find_target_data(player_t* target) const
  {
    return _target_data[target];
  }

  void priest_t::init_action_list()
  {
    if (specialization() == PRIEST_HOLY)
    {
      if (!quiet)
        sim->errorf("Player %s's role (%s) or spec(%s) is currently not supported.", name(),
          util::role_type_string(primary_role()), util::specialization_string(specialization()));
      quiet = true;
      return;
    }

    if (!action_list_str.empty())
    {
      player_t::init_action_list();
      return;
    }
    clear_action_priority_lists();

    apl_precombat();

    switch (specialization())
    {
      case PRIEST_SHADOW:
        generate_apl_shadow();
        break;
      case PRIEST_DISCIPLINE:
        if (primary_role() != ROLE_HEAL)
          generate_apl_discipline_h();
        else
          generate_apl_discipline_d();
        break;
      case PRIEST_HOLY:
        if (primary_role() != ROLE_HEAL)
          generate_apl_holy_d();
        else
          generate_apl_holy_h();
        break;
      default:
        apl_default();
        break;
    }

    use_default_action_list = true;

    base_t::init_action_list();
  }

  void priest_t::combat_begin()
  {
    player_t::combat_begin();
  }

  // priest_t::reset ==========================================================

  void priest_t::reset()
  {
    base_t::reset();

    // Reset Target Data
    for (player_t* target : sim->target_list)
    {
      if (auto td = find_target_data(target))
      {
        td->reset();
      }
    }

    insanity.reset();
  }



  /**
  * Fixup Atonement Stats HPE, HPET and HPR
  */
  void priest_t::pre_analyze_hook()
  {
    base_t::pre_analyze_hook();

    if (specs.atonement->ok())
    {
      fixup_atonement_stats("smite", "atonement_smite");
      fixup_atonement_stats("holy_fire", "atonement_holy_fire");
      fixup_atonement_stats("penance", "atonement_penance");
    }

    if (specialization() == PRIEST_DISCIPLINE || specialization() == PRIEST_HOLY)
      fixup_atonement_stats("power_word_solace", "atonement_power_word_solace");
  }

  void priest_t::target_mitigation(school_e school, dmg_e dt, action_state_t* s)
  {
    base_t::target_mitigation(school, dt, s);

    if (buffs.dispersion->check())
    {
      s->result_amount *= 1.0 + (buffs.dispersion->data().effectN(1).percent());
    }
  }

  action_t* priest_t::create_proc_action(const std::string& /*name*/, const special_effect_t& effect)
  {
    if (effect.driver()->id() == 222275)
      return new actions::spells::blessed_dawnlight_medallion_t(*this, effect);

    return nullptr;
  }

  void priest_t::create_options()
  {
    base_t::create_options();

    add_option(opt_deprecated("double_dot", "action_list=double_dot"));
    add_option(opt_bool("autounshift", options.autoUnshift));
    add_option(opt_bool("priest_fixed_time", options.priest_fixed_time));
    add_option(opt_bool("priest_ignore_healing", options.priest_ignore_healing));
    add_option(opt_bool("priest_suppress_sephuz", options.priest_suppress_sephuz));
    add_option(opt_int("priest_set_voidform_duration", options.priest_set_voidform_duration));
  }

  std::string priest_t::create_profile(save_e type)
  {
    std::string profile_str = base_t::create_profile(type);

    if (type == SAVE_ALL)
    {
      if (!options.autoUnshift)
        profile_str += "autounshift=" + util::to_string(options.autoUnshift) + "\n";

      if (!options.priest_fixed_time)
        profile_str += "priest_fixed_time=" + util::to_string(options.priest_fixed_time) + "\n";
    }

    return profile_str;
  }

  void priest_t::copy_from(player_t* source)
  {
    base_t::copy_from(source);

    priest_t* source_p = debug_cast<priest_t*>(source);

    options = source_p->options;
  }

  /**
  * Report Extension Class
  * Here you can define class specific report extensions/overrides
  */
  struct priest_report_t final : public player_report_extension_t
  {
  public:
    priest_report_t(priest_t& player) : p(player)
    {
    }

    void html_customsection(report::sc_html_stream& /* os*/) override
    {
      (void)p;
      /*// Custom Class Section
      os << "\t\t\t\t<div class=\"player-section custom_section\">\n"
      << "\t\t\t\t\t<h3 class=\"toggle open\">Custom Section</h3>\n"
      << "\t\t\t\t\t<div class=\"toggle-content\">\n";

      os << p.name();

      os << "\t\t\t\t\t\t</div>\n" << "\t\t\t\t\t</div>\n";*/
    }
  };

  struct priest_module_t final : public module_t
  {
    priest_module_t() : module_t(PRIEST)
    {
    }

    player_t* create_player(sim_t* sim, const std::string& name, race_e r = RACE_NONE) const override
    {
      auto p = new priest_t(sim, name, r);
      p->report_extension = std::unique_ptr<player_report_extension_t>(new priest_report_t(*p));
      return p;
    }
    bool valid() const override
    {
      return true;
    }
    void init(player_t* p) const override
    {
      p->buffs.guardian_spirit = buff_creator_t(p, "guardian_spirit",
        p->find_spell(47788));  // Let the ability handle the CD
      p->buffs.pain_supression = buff_creator_t(p, "pain_supression",
        p->find_spell(33206));  // Let the ability handle the CD    
    }
    void static_init() const override
    {
      items::init();
    }
    void register_hotfixes() const override
    {
      /** December 5th 2017 hotfixes

      hotfix::register_effect("Priest", "2017-12-05", "Shadow Priest damage increased by 3%", 191068)
      .field("base_value")
      .operation(hotfix::HOTFIX_SET)
      .modifier(23.0)
      .verification_value(20.0);

      hotfix::register_effect("Priest", "2017-12-05", "Shadow Priest damage increased by 3%", 179717)
      .field("base_value")
      .operation(hotfix::HOTFIX_SET)
      .modifier(23.0)
      .verification_value(20.0);

      hotfix::register_effect("Priest", "2017-12-05", "Vampiric Touch damage reduced by 15%.", 25010)
      .field("sp_coefficient")
      .operation(hotfix::HOTFIX_SET)
      .modifier(0.579)
      .verification_value(0.6816);

      hotfix::register_effect("Priest", "2017-12-05", "Shadow Word: Pain damage reduced by 15%.", 254257)
      .field("sp_coefficient")
      .operation(hotfix::HOTFIX_SET)
      .modifier(0.31)
      .verification_value(0.365);
      **/
    }

    void combat_begin(sim_t*) const override
    {
    }
    void combat_end(sim_t*) const override
    {
    }
  };

}  // PRIEST NAMESPACE