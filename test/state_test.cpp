#include "state.h"
#include "test_vectors.h"
#include <gtest/gtest.h>

using namespace mls;

class StateTest : public ::testing::Test
{
protected:
  const CipherSuite suite = CipherSuite::P256_AES128GCM_SHA256_P256;
  const SignatureScheme scheme = SignatureScheme::P256_SHA256;

  const size_t group_size = 5;
  const bytes group_id = { 0, 1, 2, 3 };
  const bytes user_id = { 4, 5, 6, 7 };
  const bytes test_message = from_hex("01020304");

  std::vector<bytes> init_secrets;
  std::vector<SignaturePrivateKey> identity_privs;
  std::vector<KeyPackage> key_packages;
  std::vector<State> states;

  StateTest()
  {
    for (size_t i = 0; i < group_size; i += 1) {
      auto init_secret = random_bytes(32);
      auto identity_priv = SignaturePrivateKey::generate(scheme);
      auto credential = Credential::basic(user_id, identity_priv.public_key());
      auto init_priv = HPKEPrivateKey::derive(suite, init_secret);
      auto key_package =
        KeyPackage{ suite, init_priv.public_key(), credential, identity_priv };

      init_secrets.push_back(init_secret);
      identity_privs.push_back(identity_priv);
      key_packages.push_back(key_package);
    }
  }

  bytes fresh_secret() const
  {
    return random_bytes(Digest(suite).output_size());
  }
};

TEST_F(StateTest, TwoPerson)
{
  // Initialize the creator's state
  auto first0 = State{
    group_id, suite, init_secrets[0], identity_privs[0], key_packages[0]
  };

  // Create an Add proposal for the new participant
  auto add = first0.add(key_packages[1]);

  // Handle the Add proposal and create a Commit
  first0.handle(add);
  auto [commit, welcome, first1] = first0.commit(fresh_secret());
  silence_unused(commit);

  // Initialize the second participant from the Welcome
  auto second0 =
    State{ init_secrets[1], identity_privs[1], key_packages[1], welcome };
  ASSERT_EQ(first1, second0);

  /// Verify that they can exchange protected messages
  auto encrypted = first1.protect(test_message);
  auto decrypted = second0.unprotect(encrypted);
  ASSERT_EQ(decrypted, test_message);
}

TEST_F(StateTest, Multi)
{
  // Initialize the creator's state
  states.emplace_back(
    group_id, suite, init_secrets[0], identity_privs[0], key_packages[0]);

  // Create and process an Add proposal for each new participant
  for (size_t i = 1; i < group_size; i += 1) {
    auto add = states[0].add(key_packages[i]);
    states[0].handle(add);
  }

  // Create a Commit that adds everybody
  auto [commit, welcome, new_state] = states[0].commit(fresh_secret());
  silence_unused(commit);
  states[0] = new_state;

  // Initialize the new joiners from the welcome
  for (size_t i = 1; i < group_size; i += 1) {
    states.emplace_back(
      init_secrets[i], identity_privs[i], key_packages[i], welcome);
  }

  // Verify that everyone can send and be received
  for (auto& state : states) {
    auto encrypted = state.protect(test_message);
    for (auto& other : states) {
      auto decrypted = other.unprotect(encrypted);
      ASSERT_EQ(decrypted, test_message);
    }
  }
}

TEST_F(StateTest, FullSize)
{
  // Initialize the creator's state
  states.emplace_back(
    group_id, suite, init_secrets[0], identity_privs[0], key_packages[0]);

  // Each participant invites the next
  for (size_t i = 1; i < group_size; i += 1) {
    auto sender = i - 1;

    auto add = states[sender].add(key_packages[i]);
    states[sender].handle(add);

    auto [commit, welcome, new_state] = states[sender].commit(fresh_secret());
    for (size_t j = 0; j < states.size(); j += 1) {
      if (j == sender) {
        states[j] = new_state;
      } else {
        states[j].handle(add);
        states[j] = states[j].handle(commit).value();
      }
    }

    states.emplace_back(
      init_secrets[i], identity_privs[i], key_packages[i], welcome);

    // Check that everyone ended up in the same place
    for (const auto& state : states) {
      ASSERT_EQ(state, states[0]);
    }

    // Check that everyone can send and be received
    for (auto& state : states) {
      auto encrypted = state.protect(test_message);
      for (auto& other : states) {
        auto decrypted = other.unprotect(encrypted);
        ASSERT_EQ(decrypted, test_message);
      }
    }
  }
}

class RunningGroupTest : public StateTest
{
protected:
  std::vector<State> states;

  RunningGroupTest()
    : StateTest()
  {
    states.emplace_back(
      group_id, suite, init_secrets[0], identity_privs[0], key_packages[0]);

    for (size_t i = 1; i < group_size; i += 1) {
      auto add = states[0].add(key_packages[i]);
      states[0].handle(add);
    }

    auto [commit, welcome, new_state] = states[0].commit(fresh_secret());
    silence_unused(commit);
    states[0] = new_state;
    for (size_t i = 1; i < group_size; i += 1) {
      states.emplace_back(
        init_secrets[i], identity_privs[i], key_packages[i], welcome);
    }
  }

  void SetUp() override { check_consistency(); }

  void check_consistency()
  {
    for (const auto& state : states) {
      ASSERT_EQ(state, states[0]);
    }
  }
};

TEST_F(RunningGroupTest, Update)
{
  for (size_t i = 0; i < group_size; i += 1) {
    auto new_leaf = fresh_secret();
    auto update = states[i].update(new_leaf);
    states[i].handle(update);
    auto [commit, welcome, new_state] = states[i].commit(new_leaf);
    silence_unused(welcome);

    for (auto& state : states) {
      if (state.index().val == i) {
        state = new_state;
      } else {
        state.handle(update);
        state = state.handle(commit).value();
      }
    }

    check_consistency();
  }
}

TEST_F(RunningGroupTest, Remove)
{
  for (int i = group_size - 2; i > 0; i -= 1) {
    auto remove = states[i].remove(LeafIndex{ uint32_t(i + 1) });
    states[i].handle(remove);
    auto [commit, welcome, new_state] = states[i].commit(fresh_secret());
    silence_unused(welcome);

    states.pop_back();
    for (auto& state : states) {
      if (state.index().val == size_t(i)) {
        state = new_state;
      } else {
        state.handle(remove);
        state = state.handle(commit).value();
      }
    }

    check_consistency();
  }
}
