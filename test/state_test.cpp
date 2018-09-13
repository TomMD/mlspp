#include "state.h"
#include <catch.hpp>

using namespace mls;

const size_t group_size = 5;
const bytes group_id{ 0, 1, 2, 3 };

TEST_CASE("Group creation", "[state]")
{
  std::vector<SignaturePrivateKey> identity_privs;
  std::vector<UserInitKey> user_init_keys;
  std::vector<bytes> init_secrets;
  std::vector<State> states;

  identity_privs.reserve(group_size);
  user_init_keys.reserve(group_size);
  init_secrets.reserve(group_size);
  states.reserve(group_size);

  auto idp = identity_privs.begin();
  auto uik = user_init_keys.begin();
  auto inp = init_secrets.begin();
  auto stp = states.begin();
  for (size_t i = 0; i < group_size; i += 1) {
    identity_privs.emplace(idp + i, SignaturePrivateKey::generate());
    auto init_secret = random_bytes(32);
    auto init_priv = DHPrivateKey::derive(init_secret);
    user_init_keys.emplace(uik + i);
    user_init_keys[i].init_keys = { init_priv.public_key() };
    user_init_keys[i].sign(identity_privs[i]);
    init_secrets.emplace(inp + i, init_secret);
  }

  SECTION("Two person, group-initiated")
  {
    // Initialize the creator's state
    states.emplace(stp, group_id, identity_privs[0]);

    // Create a GroupAdd for the new participant
    auto group_add = states[0].add(user_init_keys[1]);
    auto group_init_key = states[0].group_init_key();

    // Process the GroupAdd
    states[0] = states[0].handle(group_add);
    states.emplace(stp + 1, identity_privs[1], init_secrets[1], group_add);

    REQUIRE(states[0] == states[1]);
  }

  SECTION("Two person, user-initiated")
  {
    // Initialize the creator's state
    states.emplace(stp, group_id, identity_privs[0]);

    // Create a UserAdd for the new participant
    auto group_init_key = states[0].group_init_key();
    auto user_add =
      State::join(identity_privs[1], init_secrets[1], group_init_key);

    // Process the UserAdd
    states[0] = states[0].handle(user_add);
    states.emplace(
      stp + 1, identity_privs[1], init_secrets[1], user_add, group_init_key);
    REQUIRE(states[0] == states[1]);
  }

  SECTION("Full size, group-initiated")
  {
    // Initialize the creator's state
    states.emplace(stp, group_id, identity_privs[0]);

    // Each participant invites the next
    for (size_t i = 1; i < group_size; i += 1) {
      auto group_add = states[i - 1].add(user_init_keys[i]);
      auto group_init_key = states[i - 1].group_init_key();

      for (auto& state : states) {
        state = state.handle(group_add);
      }

      states.emplace(stp + i, identity_privs[i], init_secrets[i], group_add);

      // Check that everyone ended up in the same place
      for (const auto& state : states) {
        REQUIRE(state == states[0]);
      }
    }
  }

  SECTION("Full size, user-initiated")
  {
    // Initialize the creator's state
    states.emplace(stp, group_id, identity_privs[0]);

    // Participants add themselves in order
    for (size_t i = 1; i < group_size; i += 1) {
      auto group_init_key = states[i - 1].group_init_key();
      auto user_add =
        State::join(identity_privs[i], init_secrets[i], group_init_key);

      for (auto& state : states) {
        state = state.handle(user_add);
      }

      states.emplace(
        stp + i, identity_privs[i], init_secrets[i], user_add, group_init_key);

      // Check that everyone ended up in the same place
      for (const auto& state : states) {
        REQUIRE(state == states[0]);
      }
    }
  }
}

TEST_CASE("Operations on a running group", "[state]")
{
  std::vector<State> states;
  states.reserve(group_size);

  auto stp = states.begin();
  states.emplace(stp, group_id, SignaturePrivateKey::generate());

  for (size_t i = 1; i < group_size; i += 1) {
    auto identity_priv = SignaturePrivateKey::generate();
    auto leaf_secret = random_bytes(32);
    auto group_init_key = states[i - 1].group_init_key();
    auto user_add = State::join(identity_priv, leaf_secret, group_init_key);

    for (auto& state : states) {
      state = state.handle(user_add);
    }

    states.emplace(
      stp + i, identity_priv, leaf_secret, user_add, group_init_key);
  }

  for (const auto& state : states) {
    REQUIRE(state == states[0]);
  }

  SECTION("Each node can update its leaf key")
  {
    for (size_t i = 0; i < group_size; i += 1) {
      auto new_leaf = random_bytes(32);
      auto update = states[i].update(new_leaf);

      for (size_t j = 0; j < group_size; j += 1) {
        if (i == j) {
          states[j] = states[j].handle(update, new_leaf);
        } else {
          states[j] = states[j].handle(update);
        }
      }

      for (const auto& state : states) {
        REQUIRE(state == states[0]);
      }
    }
  }

  SECTION("Each node can remove its successor")
  {
    for (int i = group_size - 2; i > 0; i -= 1) {
      auto remove = states[i].remove(i + 1);

      for (size_t j = 0; j < i; j += 1) {
        states[j] = states[j].handle(remove);
      }

      for (size_t j = 0; j < i; j += 1) {
        REQUIRE(states[j] == states[0]);
      }
    }
  }
}
