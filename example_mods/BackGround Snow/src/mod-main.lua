local BackgroundSnow = require("src.ui.home.background-snow")

local Mod = {
	id = "aura_farm",
	name = "Aura Farm gogle",
	version = "1.0.0"
}

function Mod.onLoad(api)
	api.log("[MODDING-SYSTEM] Lua API connected")

	api.registerHomeEffect({
		create = function(width, height)
			return BackgroundSnow.create(width, height, 70)
		end,

		update = function(particles, deltaTime, width, height)
			BackgroundSnow.update(particles, deltaTime, width, height)
		end,

		draw = function(particles, renderer)
			BackgroundSnow.draw(particles, function(x, y, size, alpha, rotation)
				renderer.drawImage(
					"ui/snow-flak.png",
					x,
					y,
					size,
					alpha,
					rotation
				)
			end)
		end
	})
end

return Mod
