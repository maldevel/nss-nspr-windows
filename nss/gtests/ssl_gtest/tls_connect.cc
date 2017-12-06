/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "tls_connect.h"
#include "sslexp.h"
extern "C" {
#include "libssl_internals.h"
}

#include <iostream>

#include "databuffer.h"
#include "gtest_utils.h"
#include "scoped_ptrs.h"
#include "sslproto.h"

extern std::string g_working_dir_path;

namespace nss_test {

static const SSLProtocolVariant kTlsVariantsStreamArr[] = {ssl_variant_stream};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsStream =
        ::testing::ValuesIn(kTlsVariantsStreamArr);
static const SSLProtocolVariant kTlsVariantsDatagramArr[] = {
    ssl_variant_datagram};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsDatagram =
        ::testing::ValuesIn(kTlsVariantsDatagramArr);
static const SSLProtocolVariant kTlsVariantsAllArr[] = {ssl_variant_stream,
                                                        ssl_variant_datagram};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsAll =
        ::testing::ValuesIn(kTlsVariantsAllArr);

static const uint16_t kTlsV10Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10 =
    ::testing::ValuesIn(kTlsV10Arr);
static const uint16_t kTlsV11Arr[] = {SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11 =
    ::testing::ValuesIn(kTlsV11Arr);
static const uint16_t kTlsV12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV12 =
    ::testing::ValuesIn(kTlsV12Arr);
static const uint16_t kTlsV10V11Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0,
                                         SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10V11 =
    ::testing::ValuesIn(kTlsV10V11Arr);
static const uint16_t kTlsV10ToV12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0,
                                           SSL_LIBRARY_VERSION_TLS_1_1,
                                           SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10ToV12 =
    ::testing::ValuesIn(kTlsV10ToV12Arr);
static const uint16_t kTlsV11V12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_1,
                                         SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11V12 =
    ::testing::ValuesIn(kTlsV11V12Arr);

static const uint16_t kTlsV11PlusArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2, SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11Plus =
    ::testing::ValuesIn(kTlsV11PlusArr);
static const uint16_t kTlsV12PlusArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV12Plus =
    ::testing::ValuesIn(kTlsV12PlusArr);
static const uint16_t kTlsV13Arr[] = {SSL_LIBRARY_VERSION_TLS_1_3};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV13 =
    ::testing::ValuesIn(kTlsV13Arr);
static const uint16_t kTlsVAllArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2, SSL_LIBRARY_VERSION_TLS_1_1,
    SSL_LIBRARY_VERSION_TLS_1_0};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsVAll =
    ::testing::ValuesIn(kTlsVAllArr);

std::string VersionString(uint16_t version) {
  switch (version) {
    case 0:
      return "(no version)";
    case SSL_LIBRARY_VERSION_3_0:
      return "1.0";
    case SSL_LIBRARY_VERSION_TLS_1_0:
      return "1.0";
    case SSL_LIBRARY_VERSION_TLS_1_1:
      return "1.1";
    case SSL_LIBRARY_VERSION_TLS_1_2:
      return "1.2";
    case SSL_LIBRARY_VERSION_TLS_1_3:
      return "1.3";
    default:
      std::cerr << "Invalid version: " << version << std::endl;
      EXPECT_TRUE(false);
      return "";
  }
}

TlsConnectTestBase::TlsConnectTestBase(SSLProtocolVariant variant,
                                       uint16_t version)
    : variant_(variant),
      client_(new TlsAgent(TlsAgent::kClient, TlsAgent::CLIENT, variant_)),
      server_(new TlsAgent(TlsAgent::kServerRsa, TlsAgent::SERVER, variant_)),
      client_model_(nullptr),
      server_model_(nullptr),
      version_(version),
      expected_resumption_mode_(RESUME_NONE),
      expected_resumptions_(0),
      session_ids_(),
      expect_extended_master_secret_(false),
      expect_early_data_accepted_(false),
      skip_version_checks_(false) {
  std::string v;
  if (variant_ == ssl_variant_datagram &&
      version_ == SSL_LIBRARY_VERSION_TLS_1_1) {
    v = "1.0";
  } else {
    v = VersionString(version_);
  }
  std::cerr << "Version: " << variant_ << " " << v << std::endl;
}

TlsConnectTestBase::~TlsConnectTestBase() {}

// Check the group of each of the supported groups
void TlsConnectTestBase::CheckGroups(
    const DataBuffer& groups, std::function<void(SSLNamedGroup)> check_group) {
  DuplicateGroupChecker group_set;
  uint32_t tmp = 0;
  EXPECT_TRUE(groups.Read(0, 2, &tmp));
  EXPECT_EQ(groups.len() - 2, static_cast<size_t>(tmp));
  for (size_t i = 2; i < groups.len(); i += 2) {
    EXPECT_TRUE(groups.Read(i, 2, &tmp));
    SSLNamedGroup group = static_cast<SSLNamedGroup>(tmp);
    group_set.AddAndCheckGroup(group);
    check_group(group);
  }
}

// Check the group of each of the shares
void TlsConnectTestBase::CheckShares(
    const DataBuffer& shares, std::function<void(SSLNamedGroup)> check_group) {
  DuplicateGroupChecker group_set;
  uint32_t tmp = 0;
  EXPECT_TRUE(shares.Read(0, 2, &tmp));
  EXPECT_EQ(shares.len() - 2, static_cast<size_t>(tmp));
  size_t i;
  for (i = 2; i < shares.len(); i += 4 + tmp) {
    ASSERT_TRUE(shares.Read(i, 2, &tmp));
    SSLNamedGroup group = static_cast<SSLNamedGroup>(tmp);
    group_set.AddAndCheckGroup(group);
    check_group(group);
    ASSERT_TRUE(shares.Read(i + 2, 2, &tmp));
  }
  EXPECT_EQ(shares.len(), i);
}

void TlsConnectTestBase::CheckEpochs(uint16_t client_epoch,
                                     uint16_t server_epoch) const {
  uint16_t read_epoch = 0;
  uint16_t write_epoch = 0;

  EXPECT_EQ(SECSuccess,
            SSLInt_GetEpochs(client_->ssl_fd(), &read_epoch, &write_epoch));
  EXPECT_EQ(server_epoch, read_epoch) << "client read epoch";
  EXPECT_EQ(client_epoch, write_epoch) << "client write epoch";

  EXPECT_EQ(SECSuccess,
            SSLInt_GetEpochs(server_->ssl_fd(), &read_epoch, &write_epoch));
  EXPECT_EQ(client_epoch, read_epoch) << "server read epoch";
  EXPECT_EQ(server_epoch, write_epoch) << "server write epoch";
}

void TlsConnectTestBase::ClearStats() {
  // Clear statistics.
  SSL3Statistics* stats = SSL_GetStatistics();
  memset(stats, 0, sizeof(*stats));
}

void TlsConnectTestBase::ClearServerCache() {
  SSL_ShutdownServerSessionIDCache();
  SSLInt_ClearSelfEncryptKey();
  SSL_ConfigServerSessionIDCache(1024, 0, 0, g_working_dir_path.c_str());
}

void TlsConnectTestBase::SetUp() {
  SSL_ConfigServerSessionIDCache(1024, 0, 0, g_working_dir_path.c_str());
  SSLInt_ClearSelfEncryptKey();
  SSLInt_SetTicketLifetime(30);
  SSLInt_SetMaxEarlyDataSize(1024);
  SSL_SetupAntiReplay(1 * PR_USEC_PER_SEC, 1, 3);
  ClearStats();
  Init();
}

void TlsConnectTestBase::TearDown() {
  client_ = nullptr;
  server_ = nullptr;

  SSL_ClearSessionCache();
  SSLInt_ClearSelfEncryptKey();
  SSL_ShutdownServerSessionIDCache();
}

void TlsConnectTestBase::Init() {
  client_->SetPeer(server_);
  server_->SetPeer(client_);

  if (version_) {
    ConfigureVersion(version_);
  }
}

void TlsConnectTestBase::Reset() {
  // Take a copy of the names because they are about to disappear.
  std::string server_name = server_->name();
  std::string client_name = client_->name();
  Reset(server_name, client_name);
}

void TlsConnectTestBase::Reset(const std::string& server_name,
                               const std::string& client_name) {
  client_.reset(new TlsAgent(client_name, TlsAgent::CLIENT, variant_));
  server_.reset(new TlsAgent(server_name, TlsAgent::SERVER, variant_));
  if (skip_version_checks_) {
    client_->SkipVersionChecks();
    server_->SkipVersionChecks();
  }

  Init();
}

void TlsConnectTestBase::MakeNewServer() {
  auto replacement = std::make_shared<TlsAgent>(
      server_->name(), TlsAgent::SERVER, server_->variant());
  server_ = replacement;
  if (version_) {
    server_->SetVersionRange(version_, version_);
  }
  client_->SetPeer(server_);
  server_->SetPeer(client_);
  server_->StartConnect();
}

void TlsConnectTestBase::ExpectResumption(SessionResumptionMode expected,
                                          uint8_t num_resumptions) {
  expected_resumption_mode_ = expected;
  if (expected != RESUME_NONE) {
    client_->ExpectResumption();
    server_->ExpectResumption();
    expected_resumptions_ = num_resumptions;
  }
  EXPECT_EQ(expected_resumptions_ == 0, expected == RESUME_NONE);
}

void TlsConnectTestBase::EnsureTlsSetup() {
  EXPECT_TRUE(server_->EnsureTlsSetup(server_model_ ? server_model_->ssl_fd()
                                                    : nullptr));
  EXPECT_TRUE(client_->EnsureTlsSetup(client_model_ ? client_model_->ssl_fd()
                                                    : nullptr));
}

void TlsConnectTestBase::Handshake() {
  EnsureTlsSetup();
  client_->SetServerKeyBits(server_->server_key_bits());
  client_->Handshake();
  server_->Handshake();

  ASSERT_TRUE_WAIT((client_->state() != TlsAgent::STATE_CONNECTING) &&
                       (server_->state() != TlsAgent::STATE_CONNECTING),
                   5000);
}

void TlsConnectTestBase::EnableExtendedMasterSecret() {
  client_->EnableExtendedMasterSecret();
  server_->EnableExtendedMasterSecret();
  ExpectExtendedMasterSecret(true);
}

void TlsConnectTestBase::Connect() {
  server_->StartConnect(server_model_ ? server_model_->ssl_fd() : nullptr);
  client_->StartConnect(client_model_ ? client_model_->ssl_fd() : nullptr);
  Handshake();
  CheckConnected();
}

void TlsConnectTestBase::StartConnect() {
  server_->StartConnect(server_model_ ? server_model_->ssl_fd() : nullptr);
  client_->StartConnect(client_model_ ? client_model_->ssl_fd() : nullptr);
}

void TlsConnectTestBase::ConnectWithCipherSuite(uint16_t cipher_suite) {
  EnsureTlsSetup();
  client_->EnableSingleCipher(cipher_suite);

  Connect();
  SendReceive();

  // Check that we used the right cipher suite.
  uint16_t actual;
  EXPECT_TRUE(client_->cipher_suite(&actual));
  EXPECT_EQ(cipher_suite, actual);
  EXPECT_TRUE(server_->cipher_suite(&actual));
  EXPECT_EQ(cipher_suite, actual);
}

void TlsConnectTestBase::CheckConnected() {
  // Have the client read handshake twice to make sure we get the
  // NST and the ACK.
  if (client_->version() >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      variant_ == ssl_variant_datagram) {
    client_->Handshake();
    client_->Handshake();
    auto suites = SSLInt_CountCipherSpecs(client_->ssl_fd());
    // Verify that we dropped the client's retransmission cipher suites.
    EXPECT_EQ(2, suites) << "Client has the wrong number of suites";
    if (suites != 2) {
      SSLInt_PrintCipherSpecs("client", client_->ssl_fd());
    }
  }
  EXPECT_EQ(client_->version(), server_->version());
  if (!skip_version_checks_) {
    // Check the version is as expected
    EXPECT_EQ(std::min(client_->max_version(), server_->max_version()),
              client_->version());
  }

  EXPECT_EQ(TlsAgent::STATE_CONNECTED, client_->state());
  EXPECT_EQ(TlsAgent::STATE_CONNECTED, server_->state());

  uint16_t cipher_suite1, cipher_suite2;
  bool ret = client_->cipher_suite(&cipher_suite1);
  EXPECT_TRUE(ret);
  ret = server_->cipher_suite(&cipher_suite2);
  EXPECT_TRUE(ret);
  EXPECT_EQ(cipher_suite1, cipher_suite2);

  std::cerr << "Connected with version " << client_->version()
            << " cipher suite " << client_->cipher_suite_name() << std::endl;

  if (client_->version() < SSL_LIBRARY_VERSION_TLS_1_3) {
    // Check and store session ids.
    std::vector<uint8_t> sid_c1 = client_->session_id();
    EXPECT_EQ(32U, sid_c1.size());
    std::vector<uint8_t> sid_s1 = server_->session_id();
    EXPECT_EQ(32U, sid_s1.size());
    EXPECT_EQ(sid_c1, sid_s1);
    session_ids_.push_back(sid_c1);
  }

  CheckExtendedMasterSecret();
  CheckEarlyDataAccepted();
  CheckResumption(expected_resumption_mode_);
  client_->CheckSecretsDestroyed();
  server_->CheckSecretsDestroyed();
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type, SSLNamedGroup kea_group,
                                   SSLAuthType auth_type,
                                   SSLSignatureScheme sig_scheme) const {
  if (kea_group != ssl_grp_none) {
    client_->CheckKEA(kea_type, kea_group);
    server_->CheckKEA(kea_type, kea_group);
  }
  server_->CheckAuthType(auth_type, sig_scheme);
  client_->CheckAuthType(auth_type, sig_scheme);
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type,
                                   SSLAuthType auth_type) const {
  SSLNamedGroup group;
  switch (kea_type) {
    case ssl_kea_ecdh:
      group = ssl_grp_ec_curve25519;
      break;
    case ssl_kea_dh:
      group = ssl_grp_ffdhe_2048;
      break;
    case ssl_kea_rsa:
      group = ssl_grp_none;
      break;
    default:
      EXPECT_TRUE(false) << "unexpected KEA";
      group = ssl_grp_none;
      break;
  }

  SSLSignatureScheme scheme;
  switch (auth_type) {
    case ssl_auth_rsa_decrypt:
      scheme = ssl_sig_none;
      break;
    case ssl_auth_rsa_sign:
      if (version_ >= SSL_LIBRARY_VERSION_TLS_1_2) {
        scheme = ssl_sig_rsa_pss_sha256;
      } else {
        scheme = ssl_sig_rsa_pkcs1_sha256;
      }
      break;
    case ssl_auth_rsa_pss:
      scheme = ssl_sig_rsa_pss_sha256;
      break;
    case ssl_auth_ecdsa:
      scheme = ssl_sig_ecdsa_secp256r1_sha256;
      break;
    case ssl_auth_dsa:
      scheme = ssl_sig_dsa_sha1;
      break;
    default:
      EXPECT_TRUE(false) << "unexpected auth type";
      scheme = static_cast<SSLSignatureScheme>(0x0100);
      break;
  }
  CheckKeys(kea_type, group, auth_type, scheme);
}

void TlsConnectTestBase::CheckKeys() const {
  CheckKeys(ssl_kea_ecdh, ssl_auth_rsa_sign);
}

void TlsConnectTestBase::CheckKeysResumption(SSLKEAType kea_type,
                                             SSLNamedGroup kea_group,
                                             SSLNamedGroup original_kea_group,
                                             SSLAuthType auth_type,
                                             SSLSignatureScheme sig_scheme) {
  CheckKeys(kea_type, kea_group, auth_type, sig_scheme);
  EXPECT_TRUE(expected_resumption_mode_ != RESUME_NONE);
  client_->CheckOriginalKEA(original_kea_group);
  server_->CheckOriginalKEA(original_kea_group);
}

void TlsConnectTestBase::ConnectExpectFail() {
  StartConnect();
  Handshake();
  ASSERT_EQ(TlsAgent::STATE_ERROR, client_->state());
  ASSERT_EQ(TlsAgent::STATE_ERROR, server_->state());
}

void TlsConnectTestBase::ExpectAlert(std::shared_ptr<TlsAgent>& sender,
                                     uint8_t alert) {
  EnsureTlsSetup();
  auto receiver = (sender == client_) ? server_ : client_;
  sender->ExpectSendAlert(alert);
  receiver->ExpectReceiveAlert(alert);
}

void TlsConnectTestBase::ConnectExpectAlert(std::shared_ptr<TlsAgent>& sender,
                                            uint8_t alert) {
  ExpectAlert(sender, alert);
  ConnectExpectFail();
}

void TlsConnectTestBase::ConnectExpectFailOneSide(TlsAgent::Role failing_side) {
  StartConnect();
  client_->SetServerKeyBits(server_->server_key_bits());
  client_->Handshake();
  server_->Handshake();

  auto failing_agent = server_;
  if (failing_side == TlsAgent::CLIENT) {
    failing_agent = client_;
  }
  ASSERT_TRUE_WAIT(failing_agent->state() == TlsAgent::STATE_ERROR, 5000);
}

void TlsConnectTestBase::ConfigureVersion(uint16_t version) {
  version_ = version;
  client_->SetVersionRange(version, version);
  server_->SetVersionRange(version, version);
}

void TlsConnectTestBase::SetExpectedVersion(uint16_t version) {
  client_->SetExpectedVersion(version);
  server_->SetExpectedVersion(version);
}

void TlsConnectTestBase::DisableAllCiphers() {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  server_->DisableAllCiphers();
}

void TlsConnectTestBase::EnableOnlyStaticRsaCiphers() {
  DisableAllCiphers();

  client_->EnableCiphersByKeyExchange(ssl_kea_rsa);
  server_->EnableCiphersByKeyExchange(ssl_kea_rsa);
}

void TlsConnectTestBase::EnableOnlyDheCiphers() {
  if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
    DisableAllCiphers();
    client_->EnableCiphersByKeyExchange(ssl_kea_dh);
    server_->EnableCiphersByKeyExchange(ssl_kea_dh);
  } else {
    client_->ConfigNamedGroups(kFFDHEGroups);
    server_->ConfigNamedGroups(kFFDHEGroups);
  }
}

void TlsConnectTestBase::EnableSomeEcdhCiphers() {
  if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
    client_->EnableCiphersByAuthType(ssl_auth_ecdh_rsa);
    client_->EnableCiphersByAuthType(ssl_auth_ecdh_ecdsa);
    server_->EnableCiphersByAuthType(ssl_auth_ecdh_rsa);
    server_->EnableCiphersByAuthType(ssl_auth_ecdh_ecdsa);
  } else {
    client_->ConfigNamedGroups(kECDHEGroups);
    server_->ConfigNamedGroups(kECDHEGroups);
  }
}

void TlsConnectTestBase::ConfigureSelfEncrypt() {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey privKey;
  ASSERT_TRUE(
      TlsAgent::LoadCertificate(TlsAgent::kServerRsaDecrypt, &cert, &privKey));

  ScopedSECKEYPublicKey pubKey(CERT_ExtractPublicKey(cert.get()));
  ASSERT_TRUE(pubKey);

  EXPECT_EQ(SECSuccess,
            SSL_SetSessionTicketKeyPair(pubKey.get(), privKey.get()));
}

void TlsConnectTestBase::ConfigureSessionCache(SessionResumptionMode client,
                                               SessionResumptionMode server) {
  client_->ConfigureSessionCache(client);
  server_->ConfigureSessionCache(server);
  if ((server & RESUME_TICKET) != 0) {
    ConfigureSelfEncrypt();
  }
}

void TlsConnectTestBase::CheckResumption(SessionResumptionMode expected) {
  EXPECT_NE(RESUME_BOTH, expected);

  int resume_count = expected ? expected_resumptions_ : 0;
  int stateless_count = (expected & RESUME_TICKET) ? expected_resumptions_ : 0;

  // Note: hch == server counter; hsh == client counter.
  SSL3Statistics* stats = SSL_GetStatistics();
  EXPECT_EQ(resume_count, stats->hch_sid_cache_hits);
  EXPECT_EQ(resume_count, stats->hsh_sid_cache_hits);

  EXPECT_EQ(stateless_count, stats->hch_sid_stateless_resumes);
  EXPECT_EQ(stateless_count, stats->hsh_sid_stateless_resumes);

  if (expected != RESUME_NONE) {
    if (client_->version() < SSL_LIBRARY_VERSION_TLS_1_3) {
      // Check that the last two session ids match.
      ASSERT_EQ(1U + expected_resumptions_, session_ids_.size());
      EXPECT_EQ(session_ids_[session_ids_.size() - 1],
                session_ids_[session_ids_.size() - 2]);
    } else {
      // TLS 1.3 only uses tickets.
      EXPECT_TRUE(expected & RESUME_TICKET);
    }
  }
}

void TlsConnectTestBase::EnableAlpn() {
  client_->EnableAlpn(alpn_dummy_val_, sizeof(alpn_dummy_val_));
  server_->EnableAlpn(alpn_dummy_val_, sizeof(alpn_dummy_val_));
}

void TlsConnectTestBase::EnableAlpn(const uint8_t* val, size_t len) {
  client_->EnableAlpn(val, len);
  server_->EnableAlpn(val, len);
}

void TlsConnectTestBase::EnsureModelSockets() {
  // Make sure models agents are available.
  if (!client_model_) {
    ASSERT_EQ(server_model_, nullptr);
    client_model_.reset(
        new TlsAgent(TlsAgent::kClient, TlsAgent::CLIENT, variant_));
    server_model_.reset(
        new TlsAgent(TlsAgent::kServerRsa, TlsAgent::SERVER, variant_));
    if (skip_version_checks_) {
      client_model_->SkipVersionChecks();
      server_model_->SkipVersionChecks();
    }
  }
}

void TlsConnectTestBase::CheckAlpn(const std::string& val) {
  client_->CheckAlpn(SSL_NEXT_PROTO_SELECTED, val);
  server_->CheckAlpn(SSL_NEXT_PROTO_NEGOTIATED, val);
}

void TlsConnectTestBase::EnableSrtp() {
  client_->EnableSrtp();
  server_->EnableSrtp();
}

void TlsConnectTestBase::CheckSrtp() const {
  client_->CheckSrtp();
  server_->CheckSrtp();
}

void TlsConnectTestBase::SendReceive(size_t total) {
  ASSERT_GT(total, client_->received_bytes());
  ASSERT_GT(total, server_->received_bytes());
  client_->SendData(total - server_->received_bytes());
  server_->SendData(total - client_->received_bytes());
  Receive(total);  // Receive() is cumulative
}

// Do a first connection so we can do 0-RTT on the second one.
void TlsConnectTestBase::SetupForZeroRtt() {
  // If we don't do this, then all 0-RTT attempts will be rejected.
  SSLInt_RolloverAntiReplay();

  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  server_->Set0RttEnabled(true);  // So we signal that we allow 0-RTT.
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();

  Reset();
  StartConnect();
}

// Do a first connection so we can do resumption
void TlsConnectTestBase::SetupForResume() {
  EnsureTlsSetup();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();

  Reset();
}

void TlsConnectTestBase::ZeroRttSendReceive(
    bool expect_writable, bool expect_readable,
    std::function<bool()> post_clienthello_check) {
  const char* k0RttData = "ABCDEF";
  const PRInt32 k0RttDataLen = static_cast<PRInt32>(strlen(k0RttData));

  client_->Handshake();  // Send ClientHello.
  if (post_clienthello_check) {
    if (!post_clienthello_check()) return;
  }
  PRInt32 rv =
      PR_Write(client_->ssl_fd(), k0RttData, k0RttDataLen);  // 0-RTT write.
  if (expect_writable) {
    EXPECT_EQ(k0RttDataLen, rv);
  } else {
    EXPECT_EQ(SECFailure, rv);
  }
  server_->Handshake();  // Consume ClientHello

  std::vector<uint8_t> buf(k0RttDataLen);
  rv = PR_Read(server_->ssl_fd(), buf.data(), k0RttDataLen);  // 0-RTT read
  if (expect_readable) {
    std::cerr << "0-RTT read " << rv << " bytes\n";
    EXPECT_EQ(k0RttDataLen, rv);
  } else {
    EXPECT_EQ(SECFailure, rv);
    EXPECT_EQ(PR_WOULD_BLOCK_ERROR, PORT_GetError());
  }

  // Do a second read. this should fail.
  rv = PR_Read(server_->ssl_fd(), buf.data(), k0RttDataLen);
  EXPECT_EQ(SECFailure, rv);
  EXPECT_EQ(PR_WOULD_BLOCK_ERROR, PORT_GetError());
}

void TlsConnectTestBase::Receive(size_t amount) {
  WAIT_(client_->received_bytes() == amount &&
            server_->received_bytes() == amount,
        2000);
  ASSERT_EQ(amount, client_->received_bytes());
  ASSERT_EQ(amount, server_->received_bytes());
}

void TlsConnectTestBase::ExpectExtendedMasterSecret(bool expected) {
  expect_extended_master_secret_ = expected;
}

void TlsConnectTestBase::CheckExtendedMasterSecret() {
  client_->CheckExtendedMasterSecret(expect_extended_master_secret_);
  server_->CheckExtendedMasterSecret(expect_extended_master_secret_);
}

void TlsConnectTestBase::ExpectEarlyDataAccepted(bool expected) {
  expect_early_data_accepted_ = expected;
}

void TlsConnectTestBase::CheckEarlyDataAccepted() {
  client_->CheckEarlyDataAccepted(expect_early_data_accepted_);
  server_->CheckEarlyDataAccepted(expect_early_data_accepted_);
}

void TlsConnectTestBase::DisableECDHEServerKeyReuse() {
  server_->DisableECDHEServerKeyReuse();
}

void TlsConnectTestBase::SkipVersionChecks() {
  skip_version_checks_ = true;
  client_->SkipVersionChecks();
  server_->SkipVersionChecks();
}

// Shift the DTLS timers, to the minimum time necessary to let the next timer
// run on either client or server.  This allows tests to skip waiting without
// having timers run out of order.
void TlsConnectTestBase::ShiftDtlsTimers() {
  PRIntervalTime time_shift = PR_INTERVAL_NO_TIMEOUT;
  PRIntervalTime time;
  SECStatus rv = DTLS_GetHandshakeTimeout(client_->ssl_fd(), &time);
  if (rv == SECSuccess) {
    time_shift = time;
  }
  rv = DTLS_GetHandshakeTimeout(server_->ssl_fd(), &time);
  if (rv == SECSuccess &&
      (time < time_shift || time_shift == PR_INTERVAL_NO_TIMEOUT)) {
    time_shift = time;
  }

  if (time_shift == PR_INTERVAL_NO_TIMEOUT) {
    return;
  }

  EXPECT_EQ(SECSuccess, SSLInt_ShiftDtlsTimers(client_->ssl_fd(), time_shift));
  EXPECT_EQ(SECSuccess, SSLInt_ShiftDtlsTimers(server_->ssl_fd(), time_shift));
}

TlsConnectGeneric::TlsConnectGeneric()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectPre12::TlsConnectPre12()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectTls12::TlsConnectTls12()
    : TlsConnectTestBase(GetParam(), SSL_LIBRARY_VERSION_TLS_1_2) {}

TlsConnectTls12Plus::TlsConnectTls12Plus()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectTls13::TlsConnectTls13()
    : TlsConnectTestBase(GetParam(), SSL_LIBRARY_VERSION_TLS_1_3) {}

void TlsKeyExchangeTest::EnsureKeyShareSetup() {
  EnsureTlsSetup();
  groups_capture_ =
      std::make_shared<TlsExtensionCapture>(ssl_supported_groups_xtn);
  shares_capture_ =
      std::make_shared<TlsExtensionCapture>(ssl_tls13_key_share_xtn);
  shares_capture2_ =
      std::make_shared<TlsExtensionCapture>(ssl_tls13_key_share_xtn, true);
  std::vector<std::shared_ptr<PacketFilter>> captures = {
      groups_capture_, shares_capture_, shares_capture2_};
  client_->SetPacketFilter(std::make_shared<ChainedPacketFilter>(captures));
  capture_hrr_ = std::make_shared<TlsInspectorRecordHandshakeMessage>(
      kTlsHandshakeHelloRetryRequest);
  server_->SetPacketFilter(capture_hrr_);
}

void TlsKeyExchangeTest::ConfigNamedGroups(
    const std::vector<SSLNamedGroup>& groups) {
  client_->ConfigNamedGroups(groups);
  server_->ConfigNamedGroups(groups);
}

std::vector<SSLNamedGroup> TlsKeyExchangeTest::GetGroupDetails(
    const std::shared_ptr<TlsExtensionCapture>& capture) {
  EXPECT_TRUE(capture->captured());
  const DataBuffer& ext = capture->extension();

  uint32_t tmp = 0;
  EXPECT_TRUE(ext.Read(0, 2, &tmp));
  EXPECT_EQ(ext.len() - 2, static_cast<size_t>(tmp));
  EXPECT_TRUE(ext.len() % 2 == 0);

  std::vector<SSLNamedGroup> groups;
  for (size_t i = 1; i < ext.len() / 2; i += 1) {
    EXPECT_TRUE(ext.Read(2 * i, 2, &tmp));
    groups.push_back(static_cast<SSLNamedGroup>(tmp));
  }
  return groups;
}

std::vector<SSLNamedGroup> TlsKeyExchangeTest::GetShareDetails(
    const std::shared_ptr<TlsExtensionCapture>& capture) {
  EXPECT_TRUE(capture->captured());
  const DataBuffer& ext = capture->extension();

  uint32_t tmp = 0;
  EXPECT_TRUE(ext.Read(0, 2, &tmp));
  EXPECT_EQ(ext.len() - 2, static_cast<size_t>(tmp));

  std::vector<SSLNamedGroup> shares;
  size_t i = 2;
  while (i < ext.len()) {
    EXPECT_TRUE(ext.Read(i, 2, &tmp));
    shares.push_back(static_cast<SSLNamedGroup>(tmp));
    EXPECT_TRUE(ext.Read(i + 2, 2, &tmp));
    i += 4 + tmp;
  }
  EXPECT_EQ(ext.len(), i);
  return shares;
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares, bool expect_hrr) {
  std::vector<SSLNamedGroup> groups = GetGroupDetails(groups_capture_);
  EXPECT_EQ(expected_groups, groups);

  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    ASSERT_LT(0U, expected_shares.size());
    std::vector<SSLNamedGroup> shares = GetShareDetails(shares_capture_);
    EXPECT_EQ(expected_shares, shares);
  } else {
    EXPECT_FALSE(shares_capture_->captured());
  }

  EXPECT_EQ(expect_hrr, capture_hrr_->buffer().len() != 0);
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares) {
  CheckKEXDetails(expected_groups, expected_shares, false);
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares,
    SSLNamedGroup expected_share2) {
  CheckKEXDetails(expected_groups, expected_shares, true);

  for (auto it : expected_shares) {
    EXPECT_NE(expected_share2, it);
  }
  std::vector<SSLNamedGroup> expected_shares2 = {expected_share2};
  EXPECT_EQ(expected_shares2, GetShareDetails(shares_capture2_));
}
}  // namespace nss_test