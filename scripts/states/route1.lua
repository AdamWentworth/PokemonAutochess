-- scripts/states/route1.lua

function getMessage()
    return "Route 1 - Wild Pokemon Appeared!"
end

function getEnemies()
    return {
        {
            name = "pidgey",
            gridCol = 2,
            gridRow = 2
        },
        {
            name = "rattata",
            gridCol = 5,
            gridRow = 2
        }
    }
end