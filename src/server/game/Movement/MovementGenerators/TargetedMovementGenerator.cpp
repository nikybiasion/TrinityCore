/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ByteBuffer.h"
#include "TargetedMovementGenerator.h"
#include "Errors.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "World.h"
#include "MoveSplineInit.h"
#include "MoveSpline.h"
#include "Player.h"

#define RECHECK_DISTANCE_TIMER 100
#define TARGET_NOT_ACCESSIBLE_MAX_TIMER 5000

template<class T, typename D>
void TargetedMovementGeneratorMedium<T,D>::SetTargetLocation(T &owner)
{
    if (!Target.isValid() || !Target->IsInWorld())
        return;

    if (owner.HasUnitState(UNIT_STATE_NOT_MOVE))
        return;

    if (owner.GetTypeId() == TYPEID_UNIT && !Target->isInAccessiblePlaceFor(((Creature*)&owner)))
        return;

    float x, y, z;
    bool targetIsVictim = owner.getVictim() && owner.getVictim()->GetGUID() == Target->GetGUID();

    if (!_offset)
    {
        // to nearest contact position
        float dist = 0.0f;
        if (targetIsVictim)
            dist = owner.GetFloatValue(UNIT_FIELD_COMBATREACH) + Target->GetFloatValue(UNIT_FIELD_COMBATREACH) - Target->GetObjectSize() - owner.GetObjectSize() - 1.0f;

        if (dist < 0.5f)
            dist = 0.5f;

        if (owner.IsWithinLOSInMap(owner.getVictim()))
           Target->GetContactPoint(&owner, x, y, z, dist);
        else
           Target->GetPosition(x, y, z);
    }
    else
    {
        // to at i_offset distance from target and i_angle from target facing
        Target->GetClosePoint(x, y, z, owner.GetObjectSize(), _offset, _angle);
    }

    if (!_path)
        _path = new PathFinderMovementGenerator(&owner);

    // allow pets following their master to cheat while generating paths
    bool forceDest = (owner.GetTypeId() == TYPEID_UNIT && ((Creature*)&owner)->isPet()
                        && owner.HasUnitState(UNIT_STATE_FOLLOW));
    _path->Calculate(x, y, z, forceDest);
    if (_path->GetPathType() & PATHFIND_NOPATH)
        return;

    owner.AddUnitState(UNIT_STATE_CHASE_MOVE);
    _targetReached = false;
    _recalculateTravel = false;

    Movement::MoveSplineInit init(owner);
    init.MovebyPath(_path->GetPath());
    init.SetWalk(((D*)this)->EnableWalking());
    init.Launch();
}

template<>
void TargetedMovementGeneratorMedium<Player,ChaseMovementGenerator<Player> >::UpdateFinalDistance(float /*fDistance*/)
{
    // nothing to do for Player
}

template<>
void TargetedMovementGeneratorMedium<Player,FollowMovementGenerator<Player> >::UpdateFinalDistance(float /*fDistance*/)
{
    // nothing to do for Player
}

template<>
void TargetedMovementGeneratorMedium<Creature,ChaseMovementGenerator<Creature> >::UpdateFinalDistance(float fDistance)
{
    _offset = fDistance;
    _recalculateTravel = true;
}

template<>
void TargetedMovementGeneratorMedium<Creature,FollowMovementGenerator<Creature> >::UpdateFinalDistance(float fDistance)
{
    _offset = fDistance;
    _recalculateTravel = true;
}

template<class T, typename D>
bool TargetedMovementGeneratorMedium<T,D>::Update(T &owner, const uint32 & time_diff)
{
    if (!Target.isValid() || !Target->IsInWorld())
    {
        if (_targetSearchingTimer >= TARGET_NOT_ACCESSIBLE_MAX_TIMER)
            return false;
        else
        {
            _targetSearchingTimer += time_diff;
            return true;
        }
    }

    if (!owner.isAlive())
        return false;

    if (owner.HasUnitState(UNIT_STATE_NOT_MOVE))
    {
        owner.ClearUnitState(UNIT_STATE_CHASE_MOVE);
        return true;
    }

    // prevent movement while casting spells with cast time or channel time
    if (owner.HasUnitState(UNIT_STATE_CASTING))
    {
        if (!owner.IsStopped())
        {
            // some spells should be able to be cast while moving
            // maybe some attribute? here, check the entry of creatures useing these spells
            switch (owner.GetEntry())
            {
                case 36633: // Ice Sphere (Lich King)
                case 37562: // Volatile Ooze and Gas Cloud (Putricide)
                case 37697:
                    break;
                default:
                    owner.StopMoving();
            }
        }
        return true;
    }

    // prevent crash after creature killed pet
    if (static_cast<D*>(this)->LostTarget(owner))
    {
        owner.ClearUnitState(UNIT_STATE_CHASE_MOVE);
        if (_targetSearchingTimer >= TARGET_NOT_ACCESSIBLE_MAX_TIMER)
            return false;
        else
        {
            _targetSearchingTimer += time_diff;
            return true;
        }
    }

    _recheckDistance.Update(time_diff);
    if (_recheckDistance.Passed())
    {
        _recheckDistance.Reset(RECHECK_DISTANCE_TIMER);

        G3D::Vector3 dest = owner.movespline->FinalDestination();
        float allowed_dist = 0.0f;
        bool targetIsVictim = owner.getVictim() && owner.getVictim()->GetGUID() == Target->GetGUID();
        if (targetIsVictim)
            allowed_dist = owner.GetMeleeReach() + owner.getVictim()->GetMeleeReach();
        else
            allowed_dist = Target->GetObjectSize() + owner.GetObjectSize() + sWorld->getRate(RATE_TARGET_POS_RECALCULATION_RANGE);

        if (allowed_dist < owner.GetObjectSize())
            allowed_dist = owner.GetObjectSize();

        bool targetMoved = false;
        if (owner.GetTypeId() == TYPEID_UNIT && ((Creature*)(&owner))->CanFly())
            targetMoved = !Target->IsWithinDist3d(dest.x, dest.y, dest.z, allowed_dist);
        else
            targetMoved = !Target->IsWithinDist2d(dest.x, dest.y, allowed_dist);

        if (targetIsVictim && owner.GetTypeId() == TYPEID_UNIT && !((Creature*)&owner)->isPet())
        {
            if ((!owner.getVictim() || !owner.getVictim()->isAlive()) && owner.movespline->Finalized())
                return false;

            if (!_offset && owner.movespline->Finalized() && !owner.IsWithinMeleeRange(owner.getVictim())
                && !Target->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_PENDING_STOP))
            {
                if (_targetSearchingTimer >= TARGET_NOT_ACCESSIBLE_MAX_TIMER)
                {
                    owner.DeleteFromThreatList(owner.getVictim());
                    return false;
                }
                else
                {
                    _targetSearchingTimer += time_diff;
                    targetMoved = true;
                }
            }
            else
                _targetSearchingTimer = 0;
        }
        else
            _targetSearchingTimer = 0;

        if (targetMoved || !owner.IsWithinLOSInMap(owner.getVictim()))
            SetTargetLocation(owner);
    }

    if (owner.movespline->Finalized())
    {
        static_cast<D*>(this)->MovementInform(owner);
        if (_angle == 0.f && !owner.HasInArc(0.01f, Target.getTarget()))
            owner.SetInFront(Target.getTarget());

        if (!_targetReached)
        {
            _targetReached = true;
            static_cast<D*>(this)->ReachTarget(owner);
        }
    }
    else
    {
        if (_recalculateTravel)
            SetTargetLocation(owner);
    }
    return true;
}

//-----------------------------------------------//
template<class T>
void ChaseMovementGenerator<T>::ReachTarget(T &owner)
{
    if (owner.IsWithinMeleeRange(this->Target.getTarget()))
        owner.Attack(this->Target.getTarget(), true);
}

template<>
void ChaseMovementGenerator<Player>::Initialize(Player &owner)
{
    owner.AddUnitState(UNIT_STATE_CHASE | UNIT_STATE_CHASE_MOVE);
    SetTargetLocation(owner);
}

template<>
void ChaseMovementGenerator<Creature>::Initialize(Creature &owner)
{
    owner.SetWalk(false);
    owner.AddUnitState(UNIT_STATE_CHASE | UNIT_STATE_CHASE_MOVE);
    SetTargetLocation(owner);
}

template<class T>
void ChaseMovementGenerator<T>::Finalize(T &owner)
{
    owner.ClearUnitState(UNIT_STATE_CHASE | UNIT_STATE_CHASE_MOVE);
    if (owner.GetTypeId() == TYPEID_UNIT && !((Creature*)&owner)->isPet() && owner.isAlive())
    {
        if (!owner.isInCombat() || ( this->Target.getTarget() && !this->Target.getTarget()->isInAccessiblePlaceFor(((Creature*)&owner))))
        {
            if (owner.isInCombat())
                owner.CombatStop(true);
            owner.GetMotionMaster()->MoveTargetedHome();
        }
    }
}

template<class T>
void ChaseMovementGenerator<T>::Reset(T &owner)
{
    Initialize(owner);
}

template<class T>
void ChaseMovementGenerator<T>::MovementInform(T & /*unit*/)
{
}

template<>
void ChaseMovementGenerator<Creature>::MovementInform(Creature &unit)
{
    // Pass back the GUIDLow of the target. If it is pet's owner then PetAI will handle
    if (unit.AI())
        unit.AI()->MovementInform(CHASE_MOTION_TYPE, Target.getTarget()->GetGUIDLow());
}

//-----------------------------------------------//
template<>
bool FollowMovementGenerator<Creature>::EnableWalking() const
{
    return Target.isValid() && Target->IsWalking();
}

template<>
bool FollowMovementGenerator<Player>::EnableWalking() const
{
    return false;
}

template<>
void FollowMovementGenerator<Player>::_updateSpeed(Player &/*u*/)
{
    // nothing to do for Player
}

template<>
void FollowMovementGenerator<Creature>::_updateSpeed(Creature &u)
{
    // pet only sync speed with owner
    if (!((Creature&)u).isPet() || !Target.isValid() || Target->GetGUID() != u.GetOwnerGUID())
        return;

    u.UpdateSpeed(MOVE_RUN,true);
    u.UpdateSpeed(MOVE_WALK,true);
    u.UpdateSpeed(MOVE_SWIM,true);
}

template<>
void FollowMovementGenerator<Player>::Initialize(Player &owner)
{
    owner.AddUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);
    _updateSpeed(owner);
    SetTargetLocation(owner);
}

template<>
void FollowMovementGenerator<Creature>::Initialize(Creature &owner)
{
    owner.AddUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);
    _updateSpeed(owner);
    SetTargetLocation(owner);
}

template<class T>
void FollowMovementGenerator<T>::Finalize(T &owner)
{
    owner.ClearUnitState(UNIT_STATE_FOLLOW | UNIT_STATE_FOLLOW_MOVE);
    _updateSpeed(owner);
}

template<class T>
void FollowMovementGenerator<T>::Reset(T &owner)
{
    Initialize(owner);
}

template<class T>
void FollowMovementGenerator<T>::MovementInform(T & /*unit*/)
{
}

template<>
void FollowMovementGenerator<Creature>::MovementInform(Creature &unit)
{
    // Pass back the GUIDLow of the target. If it is pet's owner then PetAI will handle
    if (unit.AI())
        unit.AI()->MovementInform(FOLLOW_MOTION_TYPE, Target.getTarget()->GetGUIDLow());
}

//-----------------------------------------------//
template void TargetedMovementGeneratorMedium<Player,ChaseMovementGenerator<Player> >::SetTargetLocation(Player &);
template void TargetedMovementGeneratorMedium<Player,FollowMovementGenerator<Player> >::SetTargetLocation(Player &);
template void TargetedMovementGeneratorMedium<Creature,ChaseMovementGenerator<Creature> >::SetTargetLocation(Creature &);
template void TargetedMovementGeneratorMedium<Creature,FollowMovementGenerator<Creature> >::SetTargetLocation(Creature &);
template bool TargetedMovementGeneratorMedium<Player,ChaseMovementGenerator<Player> >::Update(Player &, const uint32 &);
template bool TargetedMovementGeneratorMedium<Player,FollowMovementGenerator<Player> >::Update(Player &, const uint32 &);
template bool TargetedMovementGeneratorMedium<Creature,ChaseMovementGenerator<Creature> >::Update(Creature &, const uint32 &);
template bool TargetedMovementGeneratorMedium<Creature,FollowMovementGenerator<Creature> >::Update(Creature &, const uint32 &);

template void ChaseMovementGenerator<Player>::ReachTarget(Player &);
template void ChaseMovementGenerator<Creature>::ReachTarget(Creature &);
template void ChaseMovementGenerator<Player>::Finalize(Player &);
template void ChaseMovementGenerator<Creature>::Finalize(Creature &);
template void ChaseMovementGenerator<Player>::Reset(Player &);
template void ChaseMovementGenerator<Creature>::Reset(Creature &);
template void ChaseMovementGenerator<Player>::MovementInform(Player &unit);

template void FollowMovementGenerator<Player>::Finalize(Player &);
template void FollowMovementGenerator<Creature>::Finalize(Creature &);
template void FollowMovementGenerator<Player>::Reset(Player &);
template void FollowMovementGenerator<Creature>::Reset(Creature &);
template void FollowMovementGenerator<Player>::MovementInform(Player &unit);
