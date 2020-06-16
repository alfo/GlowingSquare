<?php
/*
 *
 *
 *         _             ______
     /\   | |           |  ____|
    /  \  | | _____  __ | |__ ___  _ __ ___ _   _
   / /\ \ | |/ _ \ \/ / |  __/ _ \| '__/ _ \ | | |
  / ____ \| |  __/>  <  | | | (_) | | |  __/ |_| |
 /_/    \_\_|\___/_/\_\ |_|  \___/|_|  \___|\__, |
                                             __/ |
 Glowing Square: Unifi Display              |___/
 For PHP web server
 index.php
 *
 */

require_once('config.php');
require_once('UnifiClient.php');

function formatBytes($bytes, $precision = 2) {
    $units = array('B', 'KB', 'MB', 'GB', 'TB');
    $short_units = array('B', 'K', 'M', 'G', 'T');

    $bytes = max($bytes, 0);
    $pow = floor(($bytes ? log($bytes) : 0) / log(1000));
    $pow = min($pow, count($units) - 1);

    // Uncomment one of the following alternatives
    // $bytes /= pow(1024, $pow);
    $bytes /= (1 << (10 * $pow));

    // If we're using TB then allow for one more DP
    if ($pow == 4) $precision++;

    $number = strval(round($bytes, $precision));

    // We only have space for 4 digits max
    if (strlen($number) > 2) {
      return $number . $short_units[$pow];
    } else {
      return $number . $units[$pow];
    }

}

/**
 * the short name of the site which you wish to query
 */
$site_id = 'default';

/**
 * initialize the UniFi API connection class and log in to the controller and pull the requested data
 */
$unifi_connection = new UniFi_API\Client($controlleruser, $controllerpassword, $controllerurl, $site_id, $controllerversion);
$set_debug_mode   = $unifi_connection->set_debug($debug);
$loginresults     = $unifi_connection->login();
$clients          = $unifi_connection->list_clients();
$minute_stats     = $unifi_connection->stat_5minutes_site();
$daily_stats      = $unifi_connection->stat_daily_site();

$out = [];

/*
  Client Stats
*/
$out['clients'] = 0;
$out['guests'] = 0;
$out['wireless'] = 0;
$out['wired'] = 0;
$raw_month_tx = 0;
$raw_month_rx = 0;
$out['min_uptime'] = 9999999999;

// Count the different types of clients
foreach ($clients as $client) {

  if ($client->is_guest) $out['guests']++;
    else $out['clients']++;

  if ($client->is_wired) $out['wired']++;
    else $out['wireless']++;

  if ($client->uptime < $out['min_uptime']) {
    $out['newest'] = $client->hostname;
    $out['min_uptime'] = $client->uptime;
  }

}

/*
  Monthly Stats
*/

// Gather the monthly totals of WAN up and down
$current_month = date('m', time());

foreach ($daily_stats as $stat) {

  // The time is provided in ms, annoyingly
  $time = $stat->time / 1000;

  // Only use the data if it's from this month
  if (date('m', $time) == $current_month) {
    $raw_month_tx += $stat->{'wan-tx_bytes'};
    $raw_month_rx += $stat->{'wan-rx_bytes'};
  }

}

// Format the byte counts into something more readable
$out['month_tx'] = formatBytes($raw_month_tx, 0);
$out['month_rx'] = formatBytes($raw_month_rx, 0);

/*
  Minute-by-minute graph
*/
$out['graph'] = [];
// Each entry will be one line of pixels on the graph
// So we only want the last 64 entries
$graph_width = $_GET['width'];
$graph_height = $_GET['height'];


// The value that represents 100% height on the graph to begin with
// based on my house's maximum use per 5 min (ish)
$max_graph = 350000000;

// Use the last 100 datapoints to figure out the max height of the graph
$n = 100;
foreach (array_slice($minute_stats, -$n, $n) as $stat) {
  if ($stat->wlan_bytes > $max_graph) $max_graph = $stat->wlan_bytes;
}

// Use the display width number of datapoints to draw the graph

foreach (array_slice($minute_stats, -$graph_width, $graph_width) as $stat) {

  $pixel_height = ceil(($stat->wlan_bytes / $max_graph) * $graph_height);
  array_push($out['graph'], $pixel_height);

}

/*
  Output the data with the correct headers
*/
header('Content-Type: application/json; charset=utf-8');
echo json_encode($out);
