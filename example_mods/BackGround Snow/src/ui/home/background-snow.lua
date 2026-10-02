local BackgroundSnow = {}

local function randomBetween(minimum, maximum)
	return minimum + math.random() * (maximum - minimum)
end

function BackgroundSnow.create(width, height, count)
	width = math.max(tonumber(width) or 0, 1)
	height = math.max(tonumber(height) or 0, 1)
	count = math.max(math.floor(tonumber(count) or 70), 0)

	local particles = {}

	for index = 1, count do
		particles[index] = {
			x = randomBetween(0, width),
			y = randomBetween(0, height),
			size = randomBetween(12, 30),
			alpha = randomBetween(120, 245),
			speed = randomBetween(28, 86),
			drift = randomBetween(9, 32),
			rotation = randomBetween(0, 360),
			rotationSpeed = randomBetween(-70, 70),
			phase = randomBetween(0, math.pi * 2)
		}
	end

	return particles
end

function BackgroundSnow.update(particles, deltaTime, width, height)
	width = math.max(tonumber(width) or 0, 1)
	height = math.max(tonumber(height) or 0, 1)
	deltaTime = math.max(tonumber(deltaTime) or 0, 0)

	for _, flake in ipairs(particles) do
		flake.phase = flake.phase + deltaTime
		flake.y = flake.y + flake.speed * deltaTime
		flake.x = flake.x + math.sin(flake.phase) * flake.drift * deltaTime
		flake.rotation = flake.rotation + flake.rotationSpeed * deltaTime

		if flake.y > height + flake.size then
			flake.y = -flake.size
			flake.x = randomBetween(0, width)
		end

		if flake.x < -flake.size then
			flake.x = width + flake.size
		elseif flake.x > width + flake.size then
			flake.x = -flake.size
		end
	end
end

function BackgroundSnow.draw(particles, drawFlake)
	if type(drawFlake) ~= "function" then
		return false
	end

	for _, flake in ipairs(particles) do
		drawFlake(
			flake.x,
			flake.y,
			flake.size,
			flake.alpha,
			flake.rotation
		)
	end

	return true
end

return BackgroundSnow
