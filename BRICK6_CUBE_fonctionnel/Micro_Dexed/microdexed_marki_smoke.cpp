#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "microdexed_marki_minimal.h"

static int count_nonzero(const std::vector<int16_t> &buffer)
{
  return static_cast<int>(std::count_if(buffer.begin(), buffer.end(), [](int16_t sample) {
    return sample != 0;
  }));
}

int main()
{
  MicroDexedMarkIMinimal engine;
  if (!engine.init(48000))
  {
    std::cerr << "init failed\n";
    return 1;
  }

  engine.loadDefaultPatch();

  std::vector<int16_t> block(64);
  engine.render(block.data(), static_cast<int>(block.size()));
  const int idle_nonzero = count_nonzero(block);

  engine.noteOn(60, 100);
  int attack_nonzero = 0;
  for (int i = 0; i < 8; ++i)
  {
    engine.render(block.data(), static_cast<int>(block.size()));
    attack_nonzero += count_nonzero(block);
  }

  engine.noteOff(60);
  int release_nonzero = 0;
  for (int i = 0; i < 8; ++i)
  {
    engine.render(block.data(), static_cast<int>(block.size()));
    release_nonzero += count_nonzero(block);
  }

  std::cout << "idle_nonzero=" << idle_nonzero << "\n";
  std::cout << "attack_nonzero=" << attack_nonzero << "\n";
  std::cout << "release_nonzero=" << release_nonzero << "\n";

  if (attack_nonzero <= 0)
  {
    std::cerr << "no audio after noteOn\n";
    return 2;
  }

  return 0;
}
