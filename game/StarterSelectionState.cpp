// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "GameStateManager.h"  // We need the full definition to call popState()
#include "GameWorld.h" 
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

// ---------- Helper Functions for UI Shader & Drawing ----------

// Helper function to load shader source code from a file.
std::string loadShaderSource(const char* filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Error loading shader file: " << filePath << "\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Helper function to create a shader program from vertex and fragment shader files.
GLuint createUIShaderProgram(const char* vertexPath, const char* fragmentPath) {
    std::string vertSource = loadShaderSource(vertexPath);
    std::string fragSource = loadShaderSource(fragmentPath);

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vSource = vertSource.c_str();
    glShaderSource(vertexShader, 1, &vSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fSource = fragSource.c_str();
    glShaderSource(fragmentShader, 1, &fSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Clean up the shaders as they’re now linked into our program.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

// This function returns a UI shader program (created only once).
GLuint getUIShader() {
    static GLuint uiShader = 0;
    if (uiShader == 0) {
        uiShader = createUIShaderProgram("engine/render/ui_shader.vert", "engine/render/ui_shader.frag");
    }
    return uiShader;
}

// Helper function to draw a rectangle (card) given an SDL_Rect, a shader program, and a color.
void drawCard(const SDL_Rect& rect, GLuint shaderProgram, const glm::vec3& color) {
    float x = static_cast<float>(rect.x);
    float y = static_cast<float>(rect.y);
    float w = static_cast<float>(rect.w);
    float h = static_cast<float>(rect.h);

    float vertices[] = {
        x,   y,
        x+w, y,
        x+w, y+h,
        x,   y+h
    };
    unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glm::mat4 model = glm::mat4(1.0f);
    GLint modelLoc = glGetUniformLocation(shaderProgram, "u_Model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    GLint colorLoc = glGetUniformLocation(shaderProgram, "u_Color");
    glUniform3fv(colorLoc, 1, glm::value_ptr(color));

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &VAO);
}

// Placeholder for drawing text.
// For now, we simulate drawing text by outputting to the console,
// but you should replace this with real text rendering later.
void drawText(const std::string& text, float x, float y) {
    // This is just a placeholder.
    // std::cout << "Draw text: \"" << text << "\" at (" << x << ", " << y << ")\n";
}

// -------------------- StarterSelectionState Methods --------------------

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager), gameWorld(world), selectedStarter(StarterPokemon::None)
{
    int cardWidth  = 200;
    int cardHeight = 300;
    int spacing    = 50;
    int totalWidth = 3 * cardWidth + 2 * spacing;
    int startX     = (1280 - totalWidth) / 2;
    int startY     = (720 - cardHeight) / 2;

    bulbasaurRect  = { startX,                       startY, cardWidth, cardHeight };
    charmanderRect = { startX + cardWidth + spacing, startY, cardWidth, cardHeight };
    squirtleRect   = { startX + 2*(cardWidth+spacing), startY, cardWidth, cardHeight };
}

StarterSelectionState::~StarterSelectionState() {}

void StarterSelectionState::onEnter() {
    std::cout << "[StarterSelectionState] Entering starter selection.\n";
}

void StarterSelectionState::onExit() {
    std::cout << "[StarterSelectionState] Exiting starter selection.\n";
}

void StarterSelectionState::handleInput(SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mouseX = event.button.x;
        int mouseY = event.button.y;

        glm::vec3 ourSideSpawnPos(0.0f, 0.0f, 3.5f);

        if (isPointInRect(mouseX, mouseY, bulbasaurRect)) {
            selectedStarter = StarterPokemon::Bulbasaur;
            std::cout << "Bulbasaur selected\n";

            // (1) Spawn Bulbasaur
            gameWorld->spawnPokemon("bulbasaur", ourSideSpawnPos);

            // (2) Immediately pop this state so user can drag the new Pokémon
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, charmanderRect)) {
            selectedStarter = StarterPokemon::Charmander;
            std::cout << "Charmander selected\n";

            // (1) Spawn Charmander
            gameWorld->spawnPokemon("charmander", ourSideSpawnPos);

            // (2) Pop the state
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, squirtleRect)) {
            selectedStarter = StarterPokemon::Squirtle;
            std::cout << "Squirtle selected\n";
        
            // 1) Spawn Squirtle at some position
            gameWorld->spawnPokemon("squirtle", ourSideSpawnPos);
        
            // 2) Pop the state
            stateManager->popState();
        }        
    }

    // Keyboard-based selection for convenience (optional)
    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_1) {
            // spawn bulbasaur
            gameWorld->spawnPokemon("bulbasaur", glm::vec3(0.0f));
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_2) {
            // spawn charmander
            gameWorld->spawnPokemon("charmander", glm::vec3(0.0f));
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_3) {
            // spawn squirtle
            std::cout << "No squirtle model yet!\n";
        }
    }
}

void StarterSelectionState::update(float deltaTime) {
    // Any UI updates or animations if needed
}

void StarterSelectionState::render() {
    // Setup an orthographic projection
    glm::mat4 ortho = glm::ortho(0.0f, 1280.0f, 720.0f, 0.0f);
    GLuint uiShader = getUIShader();
    glUseProgram(uiShader);

    GLint projLoc = glGetUniformLocation(uiShader, "u_Projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    // Draw 3 colored cards
    drawCard(bulbasaurRect,  uiShader, glm::vec3(0.2f, 0.7f, 0.2f)); // green
    drawCard(charmanderRect, uiShader, glm::vec3(0.7f, 0.2f, 0.2f)); // red
    drawCard(squirtleRect,   uiShader, glm::vec3(0.2f, 0.2f, 0.7f)); // blue

    glUseProgram(0);
}