// IUpdatable.h

#pragma once

class IUpdatable {
public:
    virtual ~IUpdatable() = default;
    virtual void update(float deltaTime) = 0;
};
