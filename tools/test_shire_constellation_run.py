"""The multi-vehicle compose: one shared clock, one network per vehicle, and unchanged service hostnames."""
from pathlib import Path
import unittest

import yaml

from shire_config import load_mission
from shire_constellation_run import GSW_BASE_PORT, GSW_PORT_STRIDE, render_compose, vehicle_plan

ROLES = ("42", "gsw", "cryptolib", "director", "fsw")


class ConstellationComposeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mission = load_mission("lunar-relay")
        cls.constellation, cls.vehicles = vehicle_plan(cls.mission, "crosslink-demo", "smoke", Path("/traces"))
        cls.text = render_compose("lunar-relay", "crosslink-demo", cls.vehicles)
        cls.compose = yaml.safe_load(cls.text)
        cls.services = cls.compose["services"]

    def test_the_plan_follows_the_constellation_order_with_distinct_ports_and_trace_directories(self):
        self.assertEqual([v["id"] for v in self.vehicles], ["leaf-1", "relay", "leaf-2"])
        self.assertEqual([v["gsw_port"] for v in self.vehicles],
                         [GSW_BASE_PORT + GSW_PORT_STRIDE * i for i in range(3)])
        self.assertEqual(len({v["trace_dir"] for v in self.vehicles}), 3)
        self.assertEqual([(v["spacecraft"], v["node_id"]) for v in self.vehicles],
                         [("leaf", 101), ("relay", 100), ("leaf", 102)])

    def test_every_vehicle_has_the_five_services_and_there_is_one_shared_server(self):
        expected = {f"shire-{role}-{v['id']}" for v in self.vehicles for role in ROLES} | {"shire-server"}
        self.assertEqual(set(self.services), expected)

    def test_the_server_waits_for_two_clients_per_vehicle_and_uses_tcp(self):
        environment = self.services["shire-server"]["environment"]
        self.assertIn("NUM_CLIENTS=6", environment)
        self.assertIn("SIMULITH_SYNC_TRANSPORT=zmq", environment)
        self.assertIn("SIMULITH_PUB_BIND=tcp://0.0.0.0:50000", environment)
        self.assertIn("SIMULITH_REP_BIND=tcp://0.0.0.0:50001", environment)

    def test_client_names_are_unique_and_use_the_flight_software_prefix(self):
        names = []
        for v in self.vehicles:
            for role in ("fsw", "director"):
                env = self.services[f"shire-{role}-{v['id']}"]["environment"]
                name = next(e.split("=", 1)[1] for e in env if e.startswith("SIMULITH_CLIENT_ID="))
                self.assertEqual(name, f"shire-{role}-{v['id']}")
                self.assertIn("SIMULITH_SYNC_TRANSPORT=zmq", env)
                self.assertIn("SIMULITH_PUB_ADDR=tcp://shire-server:50000", env)
                self.assertIn("SIMULITH_REP_ADDR=tcp://shire-server:50001", env)
                names.append(name)
        self.assertEqual(len(set(names)), 6)
        self.assertEqual(sum(n.startswith("shire-fsw-") for n in names), 3)

    def test_each_vehicle_has_its_own_network_with_the_usual_hostnames(self):
        for v in self.vehicles:
            net = f"net-{v['id']}"
            self.assertIn(net, self.compose["networks"])
            for role in ROLES:
                aliases = self.services[f"shire-{role}-{v['id']}"]["networks"][net]["aliases"]
                self.assertEqual(aliases, [f"shire-{role}"])
                self.assertEqual(list(self.services[f"shire-{role}-{v['id']}"]["networks"]), [net])

    def test_the_server_is_on_every_vehicle_network_and_nothing_else_is_shared(self):
        self.assertEqual(set(self.services["shire-server"]["networks"]), {f"net-{v['id']}" for v in self.vehicles})
        for v in self.vehicles:
            self.assertEqual(self.services["shire-server"]["networks"][f"net-{v['id']}"]["aliases"], ["shire-server"])
        # Volumes are per vehicle, so one vehicle's 42 socket and archive never reach another's.
        volumes = [vol.split(":")[0] for s in self.services.values() for vol in s.get("volumes", [])]
        for name in set(volumes):
            if name.startswith(("ipc_", "gsw_data_")):
                owners = {sname for sname, s in self.services.items()
                          if any(vol.split(":")[0] == name for vol in s.get("volumes", []))}
                vehicle = name.split("_", 2)[-1] if name.startswith("gsw_data_") else name[len("ipc_"):]
                self.assertTrue(all(o.endswith(vehicle) for o in owners), (name, owners))

    def test_images_are_tagged_by_vehicle_and_the_server_uses_the_first_vehicle(self):
        for v in self.vehicles:
            for role in ROLES:
                self.assertEqual(self.services[f"shire-{role}-{v['id']}"]["image"], f"shire-{role}-lunar-relay:{v['id']}")
        self.assertEqual(self.services["shire-server"]["image"], "shire-server-lunar-relay:leaf-1")

    def test_host_ports_and_container_names_do_not_collide(self):
        ports = [p for s in self.services.values() for p in s.get("ports", [])]
        self.assertEqual(ports, [f"{GSW_BASE_PORT + GSW_PORT_STRIDE * i}:8090" for i in range(3)])
        names = [s["container_name"] for s in self.services.values()]
        self.assertEqual(len(names), len(set(names)))

    def test_dependencies_refer_to_services_that_exist(self):
        for name, service in self.services.items():
            depends = service.get("depends_on", [])
            for dependency in (depends if isinstance(depends, list) else depends):
                self.assertIn(dependency, self.services, (name, dependency))

    def test_radio_security_flags_come_from_each_vehicles_resolved_radio(self):
        for v in self.vehicles:
            env = self.services[f"shire-cryptolib-{v['id']}"]["environment"]
            self.assertIn("SHIRE_UPLINK_ENCRYPTION=1", env)
            self.assertIn("SHIRE_DOWNLINK_ENCRYPTION=1", env)

    def test_a_single_vehicle_constellation_still_renders(self):
        text = render_compose("lunar-relay", "solo", self.vehicles[:1])
        compose = yaml.safe_load(text)
        self.assertIn("NUM_CLIENTS=2", compose["services"]["shire-server"]["environment"])
        self.assertEqual(len(compose["networks"]), 1)

    def test_the_compose_name_is_scoped_to_the_constellation(self):
        self.assertEqual(self.compose["name"], "shire-crosslink-demo")


if __name__ == "__main__":
    unittest.main()
