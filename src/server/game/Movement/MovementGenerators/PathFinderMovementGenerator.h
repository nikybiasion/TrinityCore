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

#ifndef _PATH_INFO_H
#define _PATH_INFO_H

#include <ace/RW_Thread_Mutex.h>
#include <ace/Thread_Mutex.h>
#include "SharedDefines.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MoveSplineInitArgs.h"

using Movement::Vector3;
using Movement::PointsArray;

class Unit;

// 74*4.0f=296y  number_of_points*interval = max_path_len
// this is way more than actual evade range
// I think we can safely cut those down even more
#define MAX_PATH_LENGTH         74
#define MAX_POINT_PATH_LENGTH   74

#define SMOOTH_PATH_STEP_SIZE   4.0f
#define SMOOTH_PATH_SLOP        0.3f

#define VERTEX_SIZE       3
#define INVALID_POLYREF   0

enum PathType
{
    PATHFIND_BLANK          = 0x0000,   // path not built yet
    PATHFIND_NORMAL         = 0x0001,   // normal path
    PATHFIND_SHORTCUT       = 0x0002,   // travel through obstacles, terrain, air, etc (old behavior)
    PATHFIND_INCOMPLETE     = 0x0004,   // we have partial path to follow - getting closer to target
    PATHFIND_NOPATH         = 0x0008,   // no valid path at all or error in generating one
    PATHFIND_NOT_USING_PATH = 0x0010    // used when we are either flying/swiming or on map w/o mmaps
};

class PathFinderMovementGenerator
{
    public:
        PathFinderMovementGenerator(Unit const* owner);
        ~PathFinderMovementGenerator();

        // Calculate the path from owner to given destination
        // return: true if new path was calculated, false otherwise (no change needed)
        bool Calculate(float destX, float destY, float destZ, bool forceDest = false);

        // option setters - use optional
        void SetUseStrightPath(bool useStraightPath) { _useStraightPath = useStraightPath; };
        void SetPathLengthLimit(float distance) { _pointPathLimit = std::min<uint32>(uint32(distance/SMOOTH_PATH_STEP_SIZE), MAX_POINT_PATH_LENGTH); };

        // result getters
        Vector3 GetStartPosition()      const { return _startPosition; }
        Vector3 GetEndPosition()        const { return _endPosition; }
        Vector3 GetActualEndPosition()  const { return _actualEndPosition; }

        PointsArray& GetPath() { return _pathPoints; }
        PathType GetPathType() const { return _type; }

    private:

        dtPolyRef      _pathPolyRefs[MAX_PATH_LENGTH];   // array of detour polygon references
        uint32         _polyLength;                      // number of polygons in the path

        PointsArray    _pathPoints;       // our actual (x,y,z) path to the target
        PathType       _type;             // tells what kind of path this is

        bool           _useStraightPath;  // type of path will be generated
        bool           _forceDestination; // when set, we will always arrive at given point
        uint32         _pointPathLimit;   // limit point path size; min(this, MAX_POINT_PATH_LENGTH)

        Vector3        _startPosition;    // {x, y, z} of current location
        Vector3        _endPosition;      // {x, y, z} of the destination
        Vector3        _actualEndPosition;// {x, y, z} of the closest possible point to given destination

        const Unit* const       _sourceUnit;       // the unit that is moving
        const dtNavMesh*        _navMesh;          // the nav mesh
        const dtNavMeshQuery*   _navMeshQuery;     // the nav mesh query used to find the path

        dtQueryFilter _filter;                     // use single filter for all movements, update it when needed

        void _setStartPosition(Vector3 point) { _startPosition = point; }
        void _setEndPosition(Vector3 point) { _actualEndPosition = point; _endPosition = point; }
        void _setActualEndPosition(Vector3 point) { _actualEndPosition = point; }
        void NormalizePath();

        void _clear()
        {
            _polyLength = 0;
            _pathPoints.clear();
        }

        bool _inRange(const Vector3 &p1, const Vector3 &p2, float r, float h) const;
        float _dist3DSqr(const Vector3 &p1, const Vector3 &p2) const;
        bool _inRangeYZX(const float* v1, const float* v2, float r, float h) const;

        dtPolyRef _getPathPolyByPosition(const dtPolyRef *polyPath, uint32 polyPathSize, const float* point, float *distance = NULL) const;
        dtPolyRef _getPolyByLocation(const float* point, float *distance) const;
        bool _haveTile(const Vector3 &p) const;

        void _buildPolyPath(const Vector3 &startPos, const Vector3 &endPos);
        void _buildPointPath(const float *startPoint, const float *endPoint);
        void _buildShortcut();

        NavTerrain _getNavTerrain(float x, float y, float z);
        void _createFilter();
        void _updateFilter();

        // smooth path aux functions
        uint32 _fixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath,
                             const dtPolyRef* visited, uint32 nvisited);
        bool _getSteerTarget(const float* startPos, const float* endPos, float minTargetDist,
                            const dtPolyRef* path, uint32 pathSize, float* steerPos,
                            unsigned char& steerPosFlag, dtPolyRef& steerPosRef);
        dtStatus _findSmoothPath(const float* startPos, const float* endPos,
                              const dtPolyRef* polyPath, uint32 polyPathSize,
                              float* smoothPath, int* smoothPathSize, uint32 smoothPathMaxSize);
        ACE_Thread_Mutex _lock;
};

#endif
