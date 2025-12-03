//
// Created by wdzik on 29.11.2025.
//

#ifndef GRA_DUNGEON_CRAWLER_OBJECT_H
#define GRA_DUNGEON_CRAWLER_OBJECT_H
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
static const glm::ivec2 DIR8[8] = {
    { 0, -1 }, // 0: N / przód
   { 1, -1 }, // 1: NE / przód-prawo
   { 1,  0 }, // 2: E / prawo
   { 1,  1 }, // 3: SE / tył-prawo
   { 0,  1 }, // 4: S / tył
   { -1, 1 }, // 5: SW / tył-lewo
   { -1, 0 }, // 6: W / lewo
   { -1,-1 }  // 7: NW / przód-lewo
};


class Object {
public:



    glm::vec3 RenderPosition;
    int GameX;
    int GameY;

    std::string name;
    int yaw; // 0 = przód, 90 = prawo, 180 = tył, 270 = lewo
    float yawRad;
    glm::vec3 orientation; //+X = prawo −X = lewo +Z = przód −Z = tył

    Object(int x=0, int y=0, int yawAngle=0)
        : GameX(x), GameY(y), yaw(yawAngle)
    {
        UpdateOrientation();
    }

    void UpdateOrientation() {
        yawRad = yaw * 3.14159265f / 180.0f;
        orientation = glm::vec3(sin(yawRad), 0, cos(yawRad));     }
    void UpdateOrientation(int new_yaw) {
        yaw=new_yaw;
        yawRad = yaw * 3.14159265f / 180.0f;
        orientation = glm::vec3(sin(yawRad), 0, cos(yawRad));
    }
    void SetPosition(int x, int y) {
        GameX = x;
        GameY = y;
    }
    int GetDirIndex() const {
        int index = (int)round(yaw / 45.0f) % 8;
        if (index < 0) index += 8;
        return index;
    }


    glm::ivec2 GetTileInDirection(int offsetSteps = 0) const {
        int index = static_cast<int>(round(yaw / 45.0f)) % 8;
        if (index < 0) index += 8;

        index = (index + offsetSteps) % 8;
        if (index < 0) index += 8;

        glm::ivec2 d = DIR8[index];
        return glm::ivec2(GameX + d.x, GameY + d.y);
    }


};



#endif //GRA_DUNGEON_CRAWLER_OBJECT_H