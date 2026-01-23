/* -----------------------------------------------------------------------------
   bh_player_controller.h
   ----------------------------------------------------------------------------- */

#ifndef BH_PLAYER_CONTROLLER_H
#define BH_PLAYER_CONTROLLER_H

#include "../core/bh_core.h"
#include "../input/bh_input.h"
#include "../math/bh_math.h"
#include "../physics/bh_physics.h"

/* -----------------------------------------------------------------------------
   Types
   ----------------------------------------------------------------------------- */

typedef enum BH_PlayerButtons
{
    BH_IN_ATTACK = (1u << 0),
    BH_IN_JUMP = (1u << 1),
    BH_IN_DUCK = (1u << 2),
    BH_IN_FORWARD = (1u << 3),
    BH_IN_BACK = (1u << 4),
    BH_IN_USE = (1u << 5),
    BH_IN_CANCEL = (1u << 6),
    BH_IN_LEFT = (1u << 7),
    BH_IN_RIGHT = (1u << 8),
    BH_IN_MOVELEFT = (1u << 9),
    BH_IN_MOVERIGHT = (1u << 10),
    BH_IN_ATTACK2 = (1u << 11),
    BH_IN_RUN = (1u << 12),
    BH_IN_RELOAD = (1u << 13),
    BH_IN_ALT1 = (1u << 14),
    BH_IN_ALT2 = (1u << 15),
    BH_IN_SCORE = (1u << 16),
    BH_IN_SPEED = (1u << 17),
    BH_IN_WALK = (1u << 18),
    BH_IN_ZOOM = (1u << 19),
    BH_IN_WEAPON1 = (1u << 20),
    BH_IN_WEAPON2 = (1u << 21),
    BH_IN_BULLRUSH = (1u << 22),
    BH_IN_GRENADE1 = (1u << 23),
    BH_IN_GRENADE2 = (1u << 24),
} BH_PlayerButtons;

typedef enum BH_PlayerFlags
{
    BH_FL_ONGROUND = (1u << 0),
    BH_FL_DUCKING = (1u << 1),
    BH_FL_WATERJUMP = (1u << 2),
    BH_FL_ONTRAIN = (1u << 3),
    BH_FL_INRAIN = (1u << 4),
    BH_FL_FROZEN = (1u << 5),
    BH_FL_ATCONTROLS = (1u << 6),
    BH_FL_CLIENT = (1u << 7),
    BH_FL_FAKECLIENT = (1u << 8),
    BH_FL_INWATER = (1u << 9),
    BH_FL_FLY = (1u << 10),
    BH_FL_SWIM = (1u << 11),
    BH_FL_CONVEYOR = (1u << 12),
    BH_FL_NPC = (1u << 13),
    BH_FL_GODMODE = (1u << 14),
    BH_FL_NOTARGET = (1u << 15),
    BH_FL_AIMTARGET = (1u << 16),
    BH_FL_PARTIALGROUND = (1u << 17),
    BH_FL_STATICPROP = (1u << 18),
    BH_FL_GRAPHED = (1u << 19),
    BH_FL_GRENADE = (1u << 20),
    BH_FL_STEPMOVEMENT = (1u << 21),
    BH_FL_DONTTOUCH = (1u << 22),
    BH_FL_BASEVELOCITY = (1u << 23),
    BH_FL_WORLDBRUSH = (1u << 24),
    BH_FL_OBJECT = (1u << 25),
    BH_FL_KILLME = (1u << 26),
    BH_FL_ONFIRE = (1u << 27),
    BH_FL_DISSOLVING = (1u << 28),
    BH_FL_TRANSRAGDOLL = (1u << 29),
    BH_FL_UNBLOCKABLE_BY_PLAYER = (1u << 30),
} BH_PlayerFlags;

typedef enum BH_PlayerMovementMode
{
    BH_MOVEMENT_WALK,
    BH_MOVEMENT_NOCLIP,
} BH_PlayerMovementMode;

typedef struct BH_PlayerMoveData
{
    vec3 m_vecViewAngles;

    float m_flForwardMove;
    float m_flSideMove;
    float m_flUpMove;

    float m_flMaxSpeed;

    uint32_t m_nButtons;
    uint32_t m_nOldButtons;

    vec3 m_vecOrigin;
    vec3 m_vecVelocity;

    vec3 m_outWishVel;
    vec3 m_outJumpVel;
    float m_outStepHeight;
} BH_PlayerMoveData;

typedef struct BH_PlayerLocalData
{
    bool m_bDucked;
    bool m_bDucking;
    float m_flDucktime; /* ms */

    float m_flDuckJumpTime; /* s */
    bool m_bInDuckJump;

    bool m_bAllowAutoMovement;

    float m_flStepSize; /* HU */
} BH_PlayerLocalData;

typedef struct BH_PlayerState
{
    vec3 origin;
    vec3 velocity;
    vec3 view_angles_deg;
    vec3 view_offset;

    BH_PlayerMovementMode movement_mode;

    uint32_t flags;       /* BH_FL_* */
    uint32_t buttons;     /* BH_IN_* (current) */
    uint32_t old_buttons; /* BH_IN_* (previous) */

    int ground_brush_index; /* -1 if none */
    vec3 ground_normal;

    float m_surfaceFriction;
    vec3 base_velocity;
    float gravity_scale;

    int water_level; /* 0..3 */
    BH_Contents water_type;
    float water_jump_time; /* s */

    float stamina; /* ms */

    int deadflag;
    bool duck_until_on_ground;

    BH_PlayerLocalData local;
} BH_PlayerState;

typedef struct BH_GameMoveConvars
{
    float sv_accelerate;
    float sv_airaccelerate;
    float sv_friction;
    float sv_stopspeed;
    float sv_gravity;

    float sv_maxspeed;
    float sv_maxvelocity;

    float sv_bounce;
} BH_GameMoveConvars;

typedef struct BH_PlayerControllerConfig
{
    BH_GameMoveConvars sv;

    float noclip_accelerate;
    float noclip_friction;
    float noclip_speed;

    float ground_normal_min_z;
    float step_size;

    BH_Contents player_solid_mask;

    vec3 hull_mins;
    vec3 hull_maxs;
    vec3 duck_hull_mins;
    vec3 duck_hull_maxs;

    vec3 view_offset;
    vec3 duck_view_offset;
} BH_PlayerControllerConfig;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

void BH_PlayerController_InitState(BH_PlayerState *ps);

BH_PlayerControllerConfig BH_PlayerController_GetDefaultConfig(void);

void BH_PlayerController_Simulate(BH_PlayerState *player, const BH_PlayerControllerConfig *cfg,
                                  const BH_PhysicsWorld *world, const BH_Intent *intent, float dt);

#endif /* BH_PLAYER_CONTROLLER_H */
