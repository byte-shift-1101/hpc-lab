import re

bandwidth_file = "Bandwidth.txt"
latency_file = "Latency.txt"

# <platform version="4.1">
#   <zone id="world" routing="Full">
#     <cluster id="cluster-crossbar"
#              prefix="node" radical="1-10" suffix=""
#              speed="25.73Gf" bw="{bw}MBps" lat="{lat}us"/>
#   </zone>
# </platform>

# <platform version="4.1">
#     <zone id="world" routing="Full">
#         <host id="rdcn25" speed="109Gf" core="48"/>
#         <host id="rdcn26" speed="109Gf" core="48"/>
#         <link id="ib_link" bandwidth="{bw}MBps" latency="{lat}us"/>
#         <route src="rdcn25" dst="rdcn26">
#             <link_ctn id="ib_link"/>
#         </route>
#     </zone>
# </platform>
template = """<?xml version='1.0'?>
<!DOCTYPE platform SYSTEM "https://simgrid.org/simgrid.dtd">
<platform version="4.1">
  <zone id="world" routing="Full">
    <cluster id="cluster-crossbar"
             prefix="node" radical="1-10" suffix=""
             speed="25.73Gf" bw="{bw}MBps" lat="{lat}us"/>
  </zone>
</platform>
"""

def parse_file(path):
    data = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = re.split(r"\s+", line.strip())
            size = int(parts[0])
            value = float(parts[1])
            data[size] = value
    return data


bw_data = parse_file(bandwidth_file)
lat_data = parse_file(latency_file)

for size in bw_data:
    if size not in lat_data:
        continue

    bw = bw_data[size]
    lat = lat_data[size]

    content = template.format(bw=bw, lat=lat)

    with open(f"{size}.xml", "w") as f:
        f.write(content)