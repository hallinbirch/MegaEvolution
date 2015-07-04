/*
 * Custom battle script command; it handles basically the entire mega evolution
 * transformation.
 */

#include "mega.h" 
#include "types.h"
#include "common.h"
#include "battle.h"
#include "pokemon.h"
#include "strings.h"
#include "text.h"
#include "evo.h"
#include "config.h"

#define CURRENT_BANK (*b_active_side)

void set_species(u16 index);
void show_message(char *buf);
u8 *get_pokemon_data();
void buffer_str();
void special_strcpy(u8 *dest, u8 *src);

typedef void (*bxcb)(void);
void set_b_x_callback(bxcb callback);

//void move_anim_start(u8,u8,u8,u8);
void animation_script_start(u8 *script, u8 attacker, u8 defender);

extern void play_mega_evolution(u8 attacker, u8 defender);

evolution *get_evolution_data() {
  u8 *buffer_A = (u8*) (0x02022BC4 + CURRENT_BANK * 0x200);
  return (evolution*) (buffer_A[3] | (buffer_A[4] << 8) | (buffer_A[5] << 16) | (buffer_A[6] << 24));
}

battle_data *get_battle_data() {
  // TODO: Calculate correctnessssssss
  return (battle_data *) (0x02023BE4 + sizeof(battle_data) * CURRENT_BANK);
}

void healthbar_update(u8 bank);

void wait_for_message();
void AGBPrint(const char *);
void command() {
  // Wait for other Mega Evolutions to finish
  if (megadata->running) return;

  // Stop other Mega Evolutions, it's our turn
  megadata->running = 1;

  char *buffer = (char*) 0x0202298C;
  // Read species from the buffer
  evolution *evo = get_evolution_data();
	
  set_species(evo->species);
	
  // Update health box (to hide level text)
  if (CURRENT_BANK & 1) {
  } else {
    healthbar_update(CURRENT_BANK);
  }
	
  // TODO: Support no message (for primals)fg
  special_strcpy((u8*) buffer, (u8*) str_before[evo->unknown]);
  show_message(buffer);
		
  set_b_x_callback((bxcb) wait_for_message);
	
  //animation_script_start((u8*) 0x081D6594, 0, 1);
	
  //play_mega_evolution(0, 1);
	
  //move_anim_start(0, 0, 1, 5);
}

void ability_fix_cb() {
  bxcb *b_c = (bxcb*) 0x03004F84;
  bxcb *bc_backup = (bxcb*) 0x02023D78;
	
  // Call the function that does all the work
  ((bxcb) (0x801385C + 1))();
	
  // If this condition is true, the above callback finished and overwrote b_c
  // Restore the back up in this case
  if (*b_c == 0x08014040 + 1) {
    *b_c = *bc_backup;
  }
}

void ability_fix() {
  // Sets b_c to a callback that calls ability_something
  // Fixes abilities that run on enter (drought, etc.)
  // Thanks daniilS!
  u8 **dp08_ptr = (u8**) 0x02023FE8;
  u8 *dp08 = *dp08_ptr;
  bxcb *b_c = (bxcb*) 0x03004F84;
	
  // Some unused word in the memory - pick any
  bxcb *bc_backup = (bxcb*) 0x02023D78;
	
  *(dp08 + 0x4C) = 0;
  *(dp08 + 0xD9) = 0;
  *(dp08 + 0xB6) = 0;
	
  // Fix bug introduced by this - the callback we need resets b_c
  // Remember the old b_c
  *bc_backup = *b_c;
	
  // Use wrapper function
  *b_c = ability_fix_cb;
}

void set_species(u16 index) {
  u16 species = index;
  u8 *data = get_pokemon_data();
  u8 i;
	
  pokemon_setattr(data, 0xB, (u8*) &species);
  recalc_stats(data);
	
  // TODO: Update healthbar? Mega's shouldn't change health so theres no need
	
  // Update battle data
  battle_data *bdata = get_battle_data();
	
  bdata->species = species;
	
  // Update stats
  for (i = 0; i < 5; ++i) {
    bdata->stats[i] = pokemon_getattr(data, 0x3B + i, 0);
    //*((u16*)(battle_data + 2 + i * 2)) = pokemon_getattr(data, 0x3B + i, 0);
  }
	
  // We need the base stats to set the rest
  pokemon_base *base_stats = (pokemon_base*) 0x08254784;
	
  // Set ability
  // Megas only have one ability; don't bother with the second one
  bdata->ability_id = base_stats[index].ability1;
	
  // Fix abilities that activate when switching (intimidate, weather abilities, etc.) 
  // do not work
  ability_fix();
	
  // Type changes
  bdata->type1 = base_stats[index].type1;
  bdata->type2 = base_stats[index].type2;
}

char *item_name(u16 index) {
  return (char *) (0x083DB028 + index * 0x2C);
}

char *get_trainer_name() {
  if (CURRENT_BANK & 1) {
    u16 *trainer_flag = (u16*) 0x020386AE;
    u8 *trainers = (u8*) 0x0823EAC8;
  
    return trainers + *trainer_flag * 0x28 + 4;
  } else {
    return *((u8**) 0x0300500C);
  }
}

u16 get_keystone_index() {
  u16 *var;
  if (CURRENT_BANK & 1) {
    var = var_access(KEYSTONE_OPPONENT_VAR);
  } else {
    var = var_access(KEYSTONE_PLAYER_VAR);
  }

  u16 item = *var;
  return item ? item : KEYSTONE_DEFAULT;
}

char *get_species_name(u8 *pokemon_data) {
  u16 species = pokemon_getattr(pokemon_data, 0xB, 0);
  
  char *name = 0x08245EE0 + 0xB * species;
  return name;
}

void special_strcpy(u8 *dest, u8 *src) {
  u8 ch;
  u8 *data = get_pokemon_data();
  char buffer[10];
  u8* buf;
	
  battle_data *bdata = get_battle_data();
	
  while ((ch = *src++) != 0xFF) {
    // Do something different for variables
    if (ch == 0xFD) {
      switch (*src++) {
	// Pokemon's nickname
      case 0:
	buf = (u8*) buffer;
	pokemon_getattr(data, 2, buffer);
	break;
	// Held item
      case 1:
	buf = item_name(bdata->held_item);
	break;
	// Trainer's name
      case 2:
	// TODO: Load trainer name if enemy is mega evolving
	//buf = *((u8**) 0x0300500C);
	buf = get_trainer_name();
	break;
	// Trainer's accessory
      case 3:
	// TODO: Support loading accessories from a table or something
	buf = item_name(get_keystone_index());
	break;
      case 4:
	buf = get_species_name(data);
	break;
      }
			
      // Copy smaller buffer into string
      while ((*dest++ = *buf++) != 0xFF);
      dest--;
    } else {
      *dest++ = ch;
    }
  }
	
  *dest = 0xFF;
}

void exec_completed() {
  megadata->running = 0;

  if (CURRENT_BANK & 1) {
    exec_completed_tbl2();
  } else {
    exec_completed_tbl1();
  }

  // If mutliple Mega Evolutions happen in a row, this doesn't get set. So force set it
  // TODO: Neaten up
  *((u32*) 0x03004F84) = (0x080155c9);
};

void delay_before_end() {
  u16 *timer = (u16*) 0x02023D7E;
	
  if (*timer > 0) {
    (*timer)--;
  } else {
    exec_completed();
  }
}

void wait_transformation_message() {
  u16 *timer = (u16*) 0x02023D7E;

  if (!a_pressed_maybe(0)) {
    *timer = 0x3F;
    set_b_x_callback((bxcb) delay_before_end);
  }
}

void transformation_message() {
  char *buffer = (char*) 0x0202298C;
	
  evolution *evo = get_evolution_data();

  // TODO: Post transformation message
  special_strcpy((u8*) buffer, (u8*) str_after[evo->unknown]);
  show_message(buffer);
	
  set_b_x_callback((bxcb) wait_transformation_message);
}

void wait_for_animation() {
  u16 *timer = (u16*) 0x02023D7E;
  // move_anim_active_task_count
  if (*timer > 0) {
    (*timer)--;
  } else {
    if (*((u8*) 0x02037EE2) == 0) {
      set_b_x_callback((bxcb) transformation_message);
    }
  }
}

void delay_for_animation() {
  // Unused halfword - just pick any piece of padding
  u16 *timer = (u16*) 0x02023D7E;
	
  if (*timer > 0) {
    (*timer)--;
  } else {
    *timer = 0xFF;
    play_mega_evolution(CURRENT_BANK, 1);
    set_b_x_callback((bxcb) wait_for_animation);
  }
}

// Wait for message to finish printing before displaying the animation
void wait_for_message() {
  // a_pressed_maybe is called immediately after the message is finished
  // rendering
  u16 *timer = (u16*) 0x02023D7E;
	
  if (!a_pressed_maybe(0)) {
    *timer = 0x30;
    set_b_x_callback((bxcb) delay_for_animation);
  }
}

void show_message(char *buf) {
  buffer_str();

  // Set vblank
  *((u16*) 0x02022974) = 0;
  *((u16*) 0x02022976) = 0;
	
  // Display the message
  // TODO: Find out what second arg does
  battle_show_message(buf, 0x40);
}

void set_b_x_callback(bxcb callback) {
  bxcb *bx = ((bxcb*) 0x03004FE0);
  bx[CURRENT_BANK] = callback;
}

u8 *get_pokemon_data() {
  u8 *team_index_by_side = (u16*) 0x02023BCE;
  u8 *current_team;

  if (CURRENT_BANK & 1) current_team = enemy_team;
  else current_team = team;
	
  team_index_by_side += CURRENT_BANK << 1;
  current_team += (*team_index_by_side) * 100;
  return current_team;
}
