module['exports'] = function echoHttp (hook) {

  var env = hook.env;
  var data = hook.params["data"];
  var latitude = parseFloat(data.split(",")[0]);
  var longitude = parseFloat(data.split(",")[1]);

  var https = require('https');
  var querystring = require('querystring');
  var request = require('request');

  var ifttt_key = env.ifttt_key;

  var ifttt_url='https://maker.ifttt.com/trigger/shots-fired/with/key/'+ifttt_key;

  var NodeGeocoder = require('node-geocoder');
  var googlemaps_key = env.googlemaps_key;


  var options = {
  	provider: 'google'
  }

  var geocoder = NodeGeocoder(options);

  geocoder.reverse({lat:latitude, lon:longitude}, function(err, res) {

  	request({
  		uri: ifttt_url,
  		method: "POST",
  		json: {
  			// the json to send
  			value1: res[0]["extra"]["neighborhood"],
  			value2: res[0]["streetName"],
  			value3: res[0]["city"]
  		}
  	});

	});
  
};