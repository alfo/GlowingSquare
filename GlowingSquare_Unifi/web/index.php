<?php
/**
 * PHP API usage example
 *
 * contributed by: @gahujipo
 * description: example to pull connected users and their details from the UniFi controller and output the results
 *              in JSON format
 */
/**
 * include the config file (place your credentials etc there if not already present)
 * see the config.template.php file for an example
 */
require_once('config.php');
require_once('src/Client.php');

function formatBytes($bytes, $precision = 2) {
    $units = array('B', 'KB', 'MB', 'GB', 'TB');

    $bytes = max($bytes, 0);
    $pow = floor(($bytes ? log($bytes) : 0) / log(1024));
    $pow = min($pow, count($units) - 1);

    // Uncomment one of the following alternatives
    // $bytes /= pow(1024, $pow);
    $bytes /= (1 << (10 * $pow));

    return round($bytes, $precision) . $units[$pow];
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
$min_uptime = 9999999999;

// Count the different types of clients
foreach ($clients as $client) {

  $out['clients']++;

  if ($client->is_guest) $out['guests']++;
  if ($client->is_wired) $out['wired']++;
    else $out['wireless']++;

  if ($client->uptime < $min_uptime) {
    $out['newest'] = $client->hostname;
    $min_uptime = $client->uptime;
  }


}

/*
  Monthly Stats
*/

// Gather the monthly totals of WAN up and down
$current_month = date('m', time());

foreach ($daily_stats as $stat) {

  // Only use the data if it's from this month
  if (date('m', $stat->time) == $current_month) {
    $raw_month_tx += $stat->{'wan-tx_bytes'};
    $raw_month_rx += $stat->{'wan-rx_bytes'};
  }

}

// Format the byte counts into something more readable
$out['month_rx'] = formatBytes($raw_month_tx, 0);
$out['month_tx'] = formatBytes($raw_month_rx, 0);

/*
  Minute-by-minute graph
*/
$out['graph'] = [];
// Each entry will be one line of pixels on the graph
// So we only want the last 64 entries
$graph_width = $_GET['width'];
$graph_height = $_GET['height'];


// This is based on some status from my house
$max_graph = 300000000;

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


/**
 * output the results in JSON format
 */
header('Content-Type: application/json; charset=utf-8');
//echo json_encode($clients, JSON_PRETTY_PRINT);
echo json_encode($out);
