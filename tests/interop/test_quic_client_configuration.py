import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CLIENT_SOURCE = REPO_ROOT / "tools" / "quic_client.cpp"


class QuicClientConfigurationTests(unittest.TestCase):
    def test_external_peer_parameters_are_runtime_configuration(self):
        source = CLIENT_SOURCE.read_text(encoding="utf-8")

        for environment_name in (
            "FLOWQ_QUIC_PEER_HOST",
            "FLOWQ_QUIC_PEER_PORT",
            "FLOWQ_QUIC_STREAM_PAYLOAD",
            "FLOWQ_QUIC_EXPECT_ECHO",
        ):
            self.assertIn(environment_name, source)

        self.assertNotIn('session_cfg.peer = flowq::endpoint{"127.0.0.1", 4433, "hq-interop"};', source)
        self.assertNotIn('payload == "echo from aioquic"', source)


if __name__ == "__main__":
    unittest.main()
