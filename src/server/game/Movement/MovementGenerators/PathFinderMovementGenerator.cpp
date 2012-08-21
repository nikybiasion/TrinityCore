/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "PathFinderMovementGenerator.h"
#include "Map.h"
#include "Creature.h"
#include "MMapFactory.h"
#include "MMapManager.h"
#include "Log.h"

#include "DetourCommon.h"
#include "DetourNavMeshQuery.h"

////////////////// PathFinderMovementGenerator //////////////////
PathFinderMovementGenerator::PathFinderMovementGenerator(const Unit* owner) : _polyLength(0), _type(PATHFIND_BLANK),
_useStraightPath(false), _forceDestination(false), _pointPathLimit(MAX_POINT_PATH_LENGTH),
_sourceUnit(owner), _navMesh(NULL), _navMeshQuery(NULL)
{
    sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::PathFinderMovementGenerator for %u \n", _sourceUnit->GetGUIDLow());

    uint32 mapId = _sourceUnit->GetMapId();
    if (MMAP::MMapFactory::IsPathfindingEnabled(mapId))
    {
        MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
        _navMesh = mmap->GetNavMesh(mapId);
        _navMeshQuery = mmap->GetNavMeshQuery(mapId, _sourceUnit->GetInstanceId());
    }

    _createFilter();
}

PathFinderMovementGenerator::~PathFinderMovementGenerator()
{
    sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::~PathFinderMovementGenerator() for %u \n", _sourceUnit->GetGUIDLow());
}

bool PathFinderMovementGenerator::Calculate(float destX, float destY, float destZ, bool forceDest)
{
    if (!Trinity::IsValidMapCoord(destX, destY, destZ) ||
        !Trinity::IsValidMapCoord(_sourceUnit->GetPositionX(), _sourceUnit->GetPositionY(), _sourceUnit->GetPositionZ()))
        return false;

    Vector3 oldDest = GetEndPosition();
    Vector3 dest(destX, destY, destZ);
    _setEndPosition(dest);

    float x, y, z;
    _sourceUnit->GetPosition(x, y, z);
    Vector3 start(x, y, z);
    _setStartPosition(start);

    _forceDestination = forceDest;

    sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::calculate() for %u \n", _sourceUnit->GetGUIDLow());

    // make sure navMesh works - we can run on map w/o mmap
    // check if the start and end point have a .mmtile loaded (can we pass via not loaded tile on the way?)
    if (!_navMesh || !_navMeshQuery || _sourceUnit->HasUnitState(UNIT_STATE_IGNORE_PATHFINDING) ||
        !_haveTile(start) || !_haveTile(dest))
    {
        _buildShortcut();
        _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
        return true;
    }

    _updateFilter();

    // check if destination moved - if not we can optimize something here
    // we are following old, precalculated path?
    float dist = _sourceUnit->GetObjectSize();
    if (_inRange(oldDest, dest, dist, dist) && _pathPoints.size() > 2)
    {
        // our target is not moving - we just coming closer
        // we are moving on precalculated path - enjoy the ride
        sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::calculate:: precalculated path\n");

        _pathPoints.erase(_pathPoints.begin());
        return false;
    }
    else
    {
        // target moved, so we need to update the poly path
        TRINITY_GUARD(ACE_Thread_Mutex, _lock);
        _buildPolyPath(start, dest);
        return true;
    }
}

dtPolyRef PathFinderMovementGenerator::_getPathPolyByPosition(const dtPolyRef *polyPath, uint32 polyPathSize, const float* point, float *distance) const
{
    if (!polyPath || !polyPathSize)
        return INVALID_POLYREF;

    dtPolyRef nearestPoly = INVALID_POLYREF;
    float minDist2d = FLT_MAX;
    float minDist3d = 0.0f;

    for (uint32 i = 0; i < polyPathSize; ++i)
    {
        float closestPoint[VERTEX_SIZE];
        if (DT_SUCCESS != _navMeshQuery->closestPointOnPoly(polyPath[i], point, closestPoint))
            continue;

        float d = dtVdist2DSqr(point, closestPoint);
        if (d < minDist2d)
        {
            minDist2d = d;
            nearestPoly = polyPath[i];
            minDist3d = dtVdistSqr(point, closestPoint);
        }

        if(minDist2d < 1.0f) // shortcut out - close enough for us
            break;
    }

    if (distance)
        *distance = dtSqrt(minDist3d);

    return (minDist2d < 3.0f) ? nearestPoly : INVALID_POLYREF;
}

dtPolyRef PathFinderMovementGenerator::_getPolyByLocation(const float* point, float *distance) const
{
    // first we check the current path
    // if the current path doesn't contain the current poly,
    // we need to use the expensive navMesh.findNearestPoly
    dtPolyRef polyRef = _getPathPolyByPosition(_pathPolyRefs, _polyLength, point, distance);
    if (polyRef != INVALID_POLYREF)
        return polyRef;

    // we don't have it in our old path
    // try to get it by findNearestPoly()
    // first try with low search box
    float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};    // bounds of poly search area
    float closestPoint[VERTEX_SIZE] = {0.0f, 0.0f, 0.0f};
    dtStatus result = _navMeshQuery->findNearestPoly(point, extents, &_filter, &polyRef, closestPoint);
    if (DT_SUCCESS == result && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    // still nothing ..
    // try with bigger search box
    extents[1] = 200.0f;
    result = _navMeshQuery->findNearestPoly(point, extents, &_filter, &polyRef, closestPoint);
    if (DT_SUCCESS == result && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    return INVALID_POLYREF;
}

void PathFinderMovementGenerator::_buildPolyPath(const Vector3 &startPos, const Vector3 &endPos)
{
    // *** getting start/end poly logic ***

    float distToStartPoly, distToEndPoly;
    float startPoint[VERTEX_SIZE] = {startPos.y, startPos.z, startPos.x};
    float endPoint[VERTEX_SIZE] = {endPos.y, endPos.z, endPos.x};

    dtPolyRef startPoly = _getPolyByLocation(startPoint, &distToStartPoly);
    dtPolyRef endPoly = _getPolyByLocation(endPoint, &distToEndPoly);

    // we have a hole in our mesh
    // make shortcut path and mark it as NOPATH ( with flying exception )
    // its up to caller how he will use this info
    if (startPoly == INVALID_POLYREF || endPoly == INVALID_POLYREF)
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: (startPoly == 0 || endPoly == 0)\n");
        _buildShortcut();
        _type = (_sourceUnit->GetTypeId() == TYPEID_UNIT && ((Creature*)_sourceUnit)->CanFly())
                    ? PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH) : PATHFIND_NOPATH;
        return;
    }

    // we may need a better number here
    bool farFromPoly = (distToStartPoly > 7.0f || distToEndPoly > 7.0f);
    if (farFromPoly)
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: farFromPoly distToStartPoly=%.3f distToEndPoly=%.3f\n", distToStartPoly, distToEndPoly);

        bool buildShotrcut = false;
        if (_sourceUnit->GetTypeId() == TYPEID_UNIT)
        {
            Creature* owner = (Creature*)_sourceUnit;

            Vector3 p = (distToStartPoly > 7.0f) ? startPos : endPos;
            if (_sourceUnit->GetBaseMap()->IsUnderWater(p.x, p.y, p.z))
            {
                sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: underWater case\n");
                if (owner->canSwim())
                    buildShotrcut = true;
            }
            else
            {
                sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: flying case\n");
                if (owner->CanFly())
                    buildShotrcut = true;
            }
        }

        if (buildShotrcut)
        {
            _buildShortcut();
            _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
            return;
        }
        else
        {
            float closestPoint[VERTEX_SIZE];
            // we may want to use closestPointOnPolyBoundary instead
            if (DT_SUCCESS == _navMeshQuery->closestPointOnPoly(endPoly, endPoint, closestPoint))
            {
                dtVcopy(endPoint, closestPoint);
                _setActualEndPosition(Vector3(endPoint[2],endPoint[0],endPoint[1]));
            }

            _type = PATHFIND_INCOMPLETE;
        }
    }

    // *** poly path generating logic ***

    // start and end are on same polygon
    // just need to move in straight line
    if (startPoly == endPoly)
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: (startPoly == endPoly)\n");

        _buildShortcut();

        _pathPolyRefs[0] = startPoly;
        _polyLength = 1;

        _type = farFromPoly ? PATHFIND_INCOMPLETE : PATHFIND_NORMAL;
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: path type %d\n", _type);
        return;
    }

    // look for startPoly/endPoly in current path
    // TODO: we can merge it with getPathPolyByPosition() loop
    bool startPolyFound = false;
    bool endPolyFound = false;
    uint32 pathStartIndex, pathEndIndex;

    if (_polyLength)
    {
        for (pathStartIndex = 0; pathStartIndex < _polyLength; ++pathStartIndex)
        {
            // here to carch few bugs
            ASSERT(_pathPolyRefs[pathStartIndex] != INVALID_POLYREF);

            if (_pathPolyRefs[pathStartIndex] == startPoly)
            {
                startPolyFound = true;
                break;
            }
        }

        for (pathEndIndex = _polyLength-1; pathEndIndex > pathStartIndex; --pathEndIndex)
            if (_pathPolyRefs[pathEndIndex] == endPoly)
            {
                endPolyFound = true;
                break;
            }
    }

    if (startPolyFound && endPolyFound)
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: (startPolyFound && endPolyFound)\n");

        // we moved along the path and the target did not move out of our old poly-path
        // our path is a simple subpath case, we have all the data we need
        // just "cut" it out

        _polyLength = pathEndIndex - pathStartIndex + 1;
        memmove(_pathPolyRefs, _pathPolyRefs+pathStartIndex, _polyLength*sizeof(dtPolyRef));
    }
    else if (startPolyFound && !endPolyFound)
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: (startPolyFound && !endPolyFound)\n");

        // we are moving on the old path but target moved out
        // so we have atleast part of poly-path ready

        _polyLength -= pathStartIndex;

        // try to adjust the suffix of the path instead of recalculating entire length
        // at given interval the target cannot get too far from its last location
        // thus we have less poly to cover
        // sub-path of optimal path is optimal

        // take ~80% of the original length
        // TODO : play with the values here
        uint32 prefixPolyLength = uint32(_polyLength*0.8f + 0.5f);
        memmove(_pathPolyRefs, _pathPolyRefs+pathStartIndex, prefixPolyLength*sizeof(dtPolyRef));

        dtPolyRef suffixStartPoly = _pathPolyRefs[prefixPolyLength-1];

        // we need any point on our suffix start poly to generate poly-path, so we need last poly in prefix data
        float suffixEndPoint[VERTEX_SIZE];
        if (DT_SUCCESS != _navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint))
        {
            // we can hit offmesh connection as last poly - closestPointOnPoly() don't like that
            // try to recover by using prev polyref
            --prefixPolyLength;
            suffixStartPoly = _pathPolyRefs[prefixPolyLength-1];
            if (DT_SUCCESS != _navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint))
            {
                // suffixStartPoly is still invalid, error state
                _buildShortcut();
                _type = PATHFIND_NOPATH;
                return;
            }
        }

        // generate suffix
        uint32 suffixPolyLength = 0;
        dtStatus dtResult = _navMeshQuery->findPath(
                                suffixStartPoly,    // start polygon
                                endPoly,            // end polygon
                                suffixEndPoint,     // start position
                                endPoint,           // end position
                                &_filter,            // polygon search filter
                                _pathPolyRefs + prefixPolyLength - 1,    // [out] path
                                (int*)&suffixPolyLength,
                                MAX_PATH_LENGTH-prefixPolyLength);   // max number of polygons in output path

        if (!suffixPolyLength || dtResult != DT_SUCCESS)
        {
            // this is probably an error state, but we'll leave it
            // and hopefully recover on the next Update
            // we still need to copy our preffix
            sLog->outError(LOG_FILTER_MAPS, "%u's Path Build failed: 0 length path", _sourceUnit->GetGUIDLow());
        }

        sLog->outDebug(LOG_FILTER_MAPS, "++  m_polyLength=%u prefixPolyLength=%u suffixPolyLength=%u \n", _polyLength, prefixPolyLength, suffixPolyLength);

        // new path = prefix + suffix - overlap
        _polyLength = prefixPolyLength + suffixPolyLength - 1;
    }
    else
    {
        sLog->outDebug(LOG_FILTER_MAPS, "++ BuildPolyPath :: (!startPolyFound && !endPolyFound)\n");

        // either we have no path at all -> first run
        // or something went really wrong -> we aren't moving along the path to the target
        // just generate new path

        // free and invalidate old path data
        _clear();

        dtStatus dtResult = _navMeshQuery->findPath(
                startPoly,          // start polygon
                endPoly,            // end polygon
                startPoint,         // start position
                endPoint,           // end position
                &_filter,           // polygon search filter
                _pathPolyRefs,     // [out] path
                (int*)&_polyLength,
                MAX_PATH_LENGTH);   // max number of polygons in output path

        if (!_polyLength || dtResult != DT_SUCCESS)
        {
            // only happens if we passed bad data to findPath(), or navmesh is messed up
            sLog->outError(LOG_FILTER_MAPS, "%u's Path Build failed: 0 length path", _sourceUnit->GetGUIDLow());
            _buildShortcut();
            _type = PATHFIND_NOPATH;
            return;
        }
    }

    // by now we know what type of path we can get
    if (_pathPolyRefs[_polyLength - 1] == endPoly && !(_type & PATHFIND_INCOMPLETE))
        _type = PATHFIND_NORMAL;
    else
        _type = PATHFIND_INCOMPLETE;

    // generate the point-path out of our up-to-date poly-path
    _buildPointPath(startPoint, endPoint);
}

void PathFinderMovementGenerator::_buildPointPath(const float *startPoint, const float *endPoint)
{
    float pathPoints[MAX_POINT_PATH_LENGTH*VERTEX_SIZE];
    uint32 pointCount = 0;
    dtStatus dtResult = DT_FAILURE;
    if (_useStraightPath)
    {
        dtResult = _navMeshQuery->findStraightPath(
                startPoint,         // start position
                endPoint,           // end position
                _pathPolyRefs,     // current path
                _polyLength,       // lenth of current path
                pathPoints,         // [out] path corner points
                NULL,               // [out] flags
                NULL,               // [out] shortened path
                (int*)&pointCount,
                _pointPathLimit);   // maximum number of points/polygons to use
    }
    else
    {
        dtResult = _findSmoothPath(
                startPoint,         // start position
                endPoint,           // end position
                _pathPolyRefs,     // current path
                _polyLength,       // length of current path
                pathPoints,         // [out] path corner points
                (int*)&pointCount,
                _pointPathLimit);    // maximum number of points
    }

    if (pointCount < 2 || dtResult != DT_SUCCESS)
    {
        // only happens if pass bad data to findStraightPath or navmesh is broken
        // single point paths can be generated here
        // TODO : check the exact cases
        sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::BuildPointPath FAILED! path sized %d returned\n", pointCount);
        _buildShortcut();
        _type = PATHFIND_NOPATH;
        return;
    }

    _pathPoints.resize(pointCount);
    for (uint32 i = 0; i < pointCount; ++i)
        _pathPoints[i] = Vector3(pathPoints[i*VERTEX_SIZE+2], pathPoints[i*VERTEX_SIZE], pathPoints[i*VERTEX_SIZE+1]);

    NormalizePath();

    // first point is always our current location - we need the next one
    _setActualEndPosition(_pathPoints[pointCount-1]);

    // force the given destination, if needed
    if (_forceDestination &&
        (!(_type & PATHFIND_NORMAL) || !_inRange(GetEndPosition(), GetActualEndPosition(), 1.0f, 1.0f)))
    {
        // we may want to keep partial subpath
        if (_dist3DSqr(GetActualEndPosition(), GetEndPosition()) <
            0.3f * _dist3DSqr(GetStartPosition(), GetEndPosition()))
        {
            _setActualEndPosition(GetEndPosition());
            _pathPoints[_pathPoints.size()-1] = GetEndPosition();
        }
        else
        {
            _setActualEndPosition(GetEndPosition());
            _buildShortcut();
        }

        _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
    }

    sLog->outDebug(LOG_FILTER_MAPS, "++ PathFinderMovementGenerator::BuildPointPath path type %d size %d poly-size %d\n", _type, pointCount, _polyLength);
}

void PathFinderMovementGenerator::NormalizePath()
{
    for (uint32 i = 0; i < _pathPoints.size(); ++i)
        _sourceUnit->UpdateAllowedPositionZ(_pathPoints[i].x, _pathPoints[i].y, _pathPoints[i].z);
}
	
void PathFinderMovementGenerator::_buildShortcut()
{
    sLog->outDebug(LOG_FILTER_MAPS, "++ BuildShortcut :: making shortcut\n");

    _clear();

    // make two point path, our curr pos is the start, and dest is the end
    _pathPoints.resize(2);

    // set start and a default next position
    _pathPoints[0] = GetStartPosition();
    _pathPoints[1] = GetActualEndPosition();

    NormalizePath();
    _type = PATHFIND_SHORTCUT;
}

void PathFinderMovementGenerator::_createFilter()
{
    uint16 includeFlags = 0;
    uint16 excludeFlags = 0;

    if (_sourceUnit->GetTypeId() == TYPEID_UNIT)
    {
        Creature* creature = (Creature*)_sourceUnit;
        if (creature->canWalk())
            includeFlags |= NAV_GROUND;          // walk

        // creatures don't take environmental damage
        if (creature->canSwim())
            includeFlags |= (NAV_WATER | NAV_MAGMA | NAV_SLIME);           // swim
    }
    else if (_sourceUnit->GetTypeId() == TYPEID_PLAYER)
    {
        // perfect support not possible, just stay 'safe'
        includeFlags |= (NAV_GROUND | NAV_WATER);
    }

    _filter.setIncludeFlags(includeFlags);
    _filter.setExcludeFlags(excludeFlags);

    _updateFilter();
}

void PathFinderMovementGenerator::_updateFilter()
{
    // allow creatures to cheat and use different movement types if they are moved
    // forcefully into terrain they can't normally move in
    if (_sourceUnit->IsInWater() || _sourceUnit->IsUnderWater())
    {
        uint16 includedFlags = _filter.getIncludeFlags();
        includedFlags |= _getNavTerrain(_sourceUnit->GetPositionX(),
                                       _sourceUnit->GetPositionY(),
                                       _sourceUnit->GetPositionZ());

        _filter.setIncludeFlags(includedFlags);
    }
}

NavTerrain PathFinderMovementGenerator::_getNavTerrain(float x, float y, float z)
{
    LiquidData data;
    _sourceUnit->GetBaseMap()->getLiquidStatus(x, y, z, MAP_ALL_LIQUIDS, &data);

    switch (data.type_flags)
    {
        case MAP_LIQUID_TYPE_WATER:
        case MAP_LIQUID_TYPE_OCEAN:
            return NAV_WATER;
        case MAP_LIQUID_TYPE_MAGMA:
            return NAV_MAGMA;
        case MAP_LIQUID_TYPE_SLIME:
            return NAV_SLIME;
        default:
            return NAV_GROUND;
    }
}

bool PathFinderMovementGenerator::_haveTile(const Vector3 &p) const
{
    int tx, ty;
    float point[VERTEX_SIZE] = {p.y, p.z, p.x};

    _navMesh->calcTileLoc(point, &tx, &ty);
    return (_navMesh->getTileAt(tx, ty) != NULL);
}

uint32 PathFinderMovementGenerator::_fixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                               const dtPolyRef* visited, uint32 nvisited)
{
    int32 furthestPath = -1;
    int32 furthestVisited = -1;

    // Find furthest common polygon.
    for (int32 i = npath-1; i >= 0; --i)
    {
        bool found = false;
        for (int32 j = nvisited-1; j >= 0; --j)
        {
            if (path[i] == visited[j])
            {
                furthestPath = i;
                furthestVisited = j;
                found = true;
            }
        }
        if (found)
            break;
    }

    // If no intersection found just return current path.
    if (furthestPath == -1 || furthestVisited == -1)
        return npath;

    // Concatenate paths.

    // Adjust beginning of the buffer to include the visited.
    uint32 req = nvisited - furthestVisited;
    uint32 orig = uint32(furthestPath+1) < npath ? furthestPath+1 : npath;
    uint32 size = npath-orig > 0 ? npath-orig : 0;
    if (req+size > maxPath)
        size = maxPath-req;

    if (size)
        memmove(path+req, path+orig, size*sizeof(dtPolyRef));

    // Store visited
    for (uint32 i = 0; i < req; ++i)
        path[i] = visited[(nvisited-1)-i];

    return req+size;
}

bool PathFinderMovementGenerator::_getSteerTarget(const float* startPos, const float* endPos,
                              float minTargetDist, const dtPolyRef* path, uint32 pathSize,
                              float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
{
    // Find steer target.
    static const uint32 MAX_STEER_POINTS = 3;
    float steerPath[MAX_STEER_POINTS*VERTEX_SIZE];
    unsigned char steerPathFlags[MAX_STEER_POINTS];
    dtPolyRef steerPathPolys[MAX_STEER_POINTS];
    uint32 nsteerPath = 0;
    dtStatus dtResult = _navMeshQuery->findStraightPath(startPos, endPos, path, pathSize,
                                                steerPath, steerPathFlags, steerPathPolys, (int*)&nsteerPath, MAX_STEER_POINTS);
    if (!nsteerPath || DT_SUCCESS != dtResult)
        return false;

    // Find vertex far enough to steer to.
    uint32 ns = 0;
    while (ns < nsteerPath)
    {
        // Stop at Off-Mesh link or when point is further than slop away.
        if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
            !_inRangeYZX(&steerPath[ns*VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
            break;
        ns++;
    }
    // Failed to find good point to steer to.
    if (ns >= nsteerPath)
        return false;

    dtVcopy(steerPos, &steerPath[ns*VERTEX_SIZE]);
    steerPos[1] = startPos[1];  // keep Z value
    steerPosFlag = steerPathFlags[ns];
    steerPosRef = steerPathPolys[ns];

    return true;
}

dtStatus PathFinderMovementGenerator::_findSmoothPath(const float* startPos, const float* endPos,
                                     const dtPolyRef* polyPath, uint32 polyPathSize,
                                     float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
{
    *smoothPathSize = 0;
    uint32 nsmoothPath = 0;

    dtPolyRef polys[MAX_PATH_LENGTH];
    memcpy(polys, polyPath, sizeof(dtPolyRef)*polyPathSize);
    uint32 npolys = polyPathSize;

    float iterPos[VERTEX_SIZE], targetPos[VERTEX_SIZE];
    if (DT_SUCCESS != _navMeshQuery->closestPointOnPolyBoundary(polys[0], startPos, iterPos))
        return DT_FAILURE;

    if (DT_SUCCESS != _navMeshQuery->closestPointOnPolyBoundary(polys[npolys-1], endPos, targetPos))
        return DT_FAILURE;

    dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
    nsmoothPath++;

    // Move towards target a small advancement at a time until target reached or
    // when ran out of memory to store the path.
    while (npolys && nsmoothPath < maxSmoothPathSize)
    {
        // Find location to steer towards.
        float steerPos[VERTEX_SIZE];
        unsigned char steerPosFlag;
        dtPolyRef steerPosRef = INVALID_POLYREF;

        if (!_getSteerTarget(iterPos, targetPos, SMOOTH_PATH_SLOP, polys, npolys, steerPos, steerPosFlag, steerPosRef))
            break;

        bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END);
        bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION);

        // Find movement delta.
        float delta[VERTEX_SIZE];
        dtVsub(delta, steerPos, iterPos);
        float len = dtSqrt(dtVdot(delta,delta));
        // If the steer target is end of path or off-mesh link, do not move past the location.
        if ((endOfPath || offMeshConnection) && len < SMOOTH_PATH_STEP_SIZE)
            len = 1.0f;
        else
            len = SMOOTH_PATH_STEP_SIZE / len;

        float moveTgt[VERTEX_SIZE];
        dtVmad(moveTgt, iterPos, delta, len);

        // Move
        float result[VERTEX_SIZE];
        const static uint32 MAX_VISIT_POLY = 16;
        dtPolyRef visited[MAX_VISIT_POLY];

        uint32 nvisited = 0;
        _navMeshQuery->moveAlongSurface(polys[0], iterPos, moveTgt, &_filter, result, visited, (int*)&nvisited, MAX_VISIT_POLY);
        npolys = _fixupCorridor(polys, npolys, MAX_PATH_LENGTH, visited, nvisited);

        _navMeshQuery->getPolyHeight(polys[0], result, &result[1]);
        result[1] += 0.5f;
        dtVcopy(iterPos, result);

        // Handle end of path and off-mesh links when close enough.
        if (endOfPath && _inRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Reached end of path.
            dtVcopy(iterPos, targetPos);
            if (nsmoothPath < maxSmoothPathSize)
            {
                dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
                nsmoothPath++;
            }
            break;
        }
        else if (offMeshConnection && _inRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Advance the path up to and over the off-mesh connection.
            dtPolyRef prevRef = INVALID_POLYREF;
            dtPolyRef polyRef = polys[0];
            uint32 npos = 0;
            while (npos < npolys && polyRef != steerPosRef)
            {
                prevRef = polyRef;
                polyRef = polys[npos];
                npos++;
            }

            for (uint32 i = npos; i < npolys; ++i)
                polys[i-npos] = polys[i];

            npolys -= npos;

            // Handle the connection.
            float startPos[VERTEX_SIZE], endPos[VERTEX_SIZE];
            if (DT_SUCCESS == _navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, startPos, endPos))
            {
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], startPos);
                    nsmoothPath++;
                }
                // Move position at the other side of the off-mesh link.
                dtVcopy(iterPos, endPos);
                _navMeshQuery->getPolyHeight(polys[0], iterPos, &iterPos[1]);
                iterPos[1] += 0.5f;
            }
        }

        // Store results.
        if (nsmoothPath < maxSmoothPathSize)
        {
            dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
            nsmoothPath++;
        }
    }

    *smoothPathSize = nsmoothPath;

    // this is most likely a loop
    return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;
}

bool PathFinderMovementGenerator::_inRangeYZX(const float* v1, const float* v2, float r, float h) const
{
    const float dx = v2[0] - v1[0];
    const float dy = v2[1] - v1[1]; // elevation
    const float dz = v2[2] - v1[2];
    return (dx*dx + dz*dz) < r*r && fabsf(dy) < h;
}

bool PathFinderMovementGenerator::_inRange(const Vector3 &p1, const Vector3 &p2, float r, float h) const
{
    Vector3 d = p1-p2;
    return (d.x*d.x + d.y*d.y) < r*r && fabsf(d.z) < h;
}

float PathFinderMovementGenerator::_dist3DSqr(const Vector3 &p1, const Vector3 &p2) const
{
    return (p1-p2).squaredLength();
}
