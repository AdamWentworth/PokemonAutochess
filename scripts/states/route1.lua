function getMessage()
    return "Route 1 - Wild Pidgey Appeared!"
end

function getEnemies()
    return {
        { 
            name = "pidgey",
            x = 0.0,
            y = 0.0,
            z = -4.5  -- Position in front of the player's board
        },
        { 
            name = "rattata",
            x = 1.2,  -- Adjust positions as needed
            y = 0.0,
            z = -4.5
        }
    }
end