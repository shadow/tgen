/*
 * See LICENSE for licensing information
 */

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

gint tgenconfig_gethostname(gchar* name, size_t len) {
    gchar* tgenip = getenv("TGENHOSTNAME");
    if (tgenip != NULL) {
        return -(g_snprintf(name, len, "%s", tgenip) < 0);
    } else {
        return gethostname(name, len);
    }
}

gchar* tgenconfig_getIP() {
    return getenv("TGENIP");
}

gchar* tgenconfig_getSOCKS() {
    return getenv("TGENSOCKS");
}

const gchar* tgenconfig_getDefaultPacketMarkovModelString() {
    return  "<?xml version=\"1.0\" encoding=\"utf-8\"?><graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">"
            "  <key attr.name=\"param_rate\" attr.type=\"int\" for=\"edge\" id=\"d5\" />"
            "  <key attr.name=\"distribution\" attr.type=\"string\" for=\"edge\" id=\"d4\" />"
            "  <key attr.name=\"weight\" attr.type=\"double\" for=\"edge\" id=\"d3\" />"
            "  <key attr.name=\"type\" attr.type=\"string\" for=\"edge\" id=\"d2\" />"
            "  <key attr.name=\"name\" attr.type=\"string\" for=\"node\" id=\"d1\" />"
            "  <key attr.name=\"type\" attr.type=\"string\" for=\"node\" id=\"d0\" />"
            "  <graph edgedefault=\"directed\">"
            "    <node id=\"s1\">"
            "      <data key=\"d0\">state</data>"
            "      <data key=\"d1\">nonstop</data>"
            "    </node>"
            "    <node id=\"s0\">"
            "      <data key=\"d0\">state</data>"
            "      <data key=\"d1\">start</data>"
            "    </node>"
            "    <node id=\"o2\">"
            "      <data key=\"d0\">observation</data>"
            "      <data key=\"d1\">-</data>"
            "    </node>"
            "    <node id=\"o1\">"
            "      <data key=\"d0\">observation</data>"
            "      <data key=\"d1\">+</data>"
            "    </node>"
            "    <edge source=\"s1\" target=\"s1\">"
            "      <data key=\"d2\">transition</data>"
            "      <data key=\"d3\">1.0</data>"
            "    </edge>"
            "    <edge source=\"s1\" target=\"o2\">"
            "      <data key=\"d4\">exponential</data>"
            "      <data key=\"d2\">emission</data>"
            "      <data key=\"d5\">4294967295</data>"
            "      <data key=\"d3\">0.5</data>"
            "    </edge>"
            "    <edge source=\"s1\" target=\"o1\">"
            "      <data key=\"d4\">exponential</data>"
            "      <data key=\"d2\">emission</data>"
            "      <data key=\"d5\">4294967295</data>"
            "      <data key=\"d3\">0.5</data>"
            "    </edge>"
            "    <edge source=\"s0\" target=\"s1\">"
            "      <data key=\"d2\">transition</data>"
            "      <data key=\"d3\">1.0</data>"
            "    </edge>"
            "  </graph>"
            "</graphml>";
}

const gchar* tgenconfig_getDefaultStreamMarkovModelString() {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?><graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">"
            "  <key attr.name=\"param_location\" attr.type=\"int\" for=\"edge\" id=\"d6\" />"
            "  <key attr.name=\"param_scale\" attr.type=\"int\" for=\"edge\" id=\"d5\" />"
            "  <key attr.name=\"distribution\" attr.type=\"string\" for=\"edge\" id=\"d4\" />"
            "  <key attr.name=\"weight\" attr.type=\"double\" for=\"edge\" id=\"d3\" />"
            "  <key attr.name=\"type\" attr.type=\"string\" for=\"edge\" id=\"d2\" />"
            "  <key attr.name=\"name\" attr.type=\"string\" for=\"node\" id=\"d1\" />"
            "  <key attr.name=\"type\" attr.type=\"string\" for=\"node\" id=\"d0\" />"
            "  <graph edgedefault=\"directed\">"
            "    <node id=\"s1\">"
            "      <data key=\"d0\">state</data>"
            "      <data key=\"d1\">default</data>"
            "    </node>"
            "    <node id=\"s0\">"
            "      <data key=\"d0\">state</data>"
            "      <data key=\"d1\">start</data>"
            "    </node>"
            "    <node id=\"o1\">"
            "      <data key=\"d0\">observation</data>"
            "      <data key=\"d1\">+</data>"
            "    </node>"
            "    <edge source=\"s1\" target=\"s1\">"
            "      <data key=\"d2\">transition</data>"
            "      <data key=\"d3\">1.0</data>"
            "    </edge>"
            "    <edge source=\"s1\" target=\"o1\">"
            "      <data key=\"d4\">normal</data>"
            "      <data key=\"d5\">4000000</data>"
            "      <data key=\"d2\">emission</data>"
            "      <data key=\"d6\">10000000</data>"
            "      <data key=\"d3\">1.0</data>"
            "    </edge>"
            "    <edge source=\"s0\" target=\"s1\">"
            "      <data key=\"d2\">transition</data>"
            "      <data key=\"d3\">1.0</data>"
            "    </edge>"
            "  </graph>"
            "</graphml>";
}
