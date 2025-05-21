// Event.h

#pragma once
#include <string>

// Define an enum for event types. Expand this as needed.
enum class EventType {
    KeyDown,
    KeyUp,
    MouseMoved,
    MouseButtonDown,
    MouseButtonUp,
    RoundPhaseChanged
};

// Base event class.
class Event {
public:
    Event(EventType type) : type(type) {}
    virtual ~Event() = default;
    EventType getType() const { return type; }
    virtual std::string toString() const { return "Generic Event"; }
private:
    EventType type;
};

// --- New Specialized Event Classes ---

class MouseButtonDownEvent : public Event {
public:
    MouseButtonDownEvent(int x, int y)
        : Event(EventType::MouseButtonDown), x(x), y(y) {}
    int getX() const { return x; }
    int getY() const { return y; }
    std::string toString() const override {
        return "MouseButtonDownEvent at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
private:
    int x, y;
};

class MouseButtonUpEvent : public Event {
public:
    MouseButtonUpEvent(int x, int y)
        : Event(EventType::MouseButtonUp), x(x), y(y) {}
    int getX() const { return x; }
    int getY() const { return y; }
    std::string toString() const override {
        return "MouseButtonUpEvent at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
private:
    int x, y;
};

class MouseMotionEvent : public Event {
public:
    MouseMotionEvent(int x, int y)
        : Event(EventType::MouseMoved), x(x), y(y) {}
    int getX() const { return x; }
    int getY() const { return y; }
    std::string toString() const override {
        return "MouseMotionEvent at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
private:
    int x, y;
};
