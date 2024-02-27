const MAX_ARR_ELEMS = 220; // Should be send as configuration by server
const TEMPERATURE_IDX = 1;
const HUMIDITY_IDX = 2;

var gData = {};
var gSensorIDsToNames = {};

async function fetchSensorsIDToNameMapping() {
    const response = await fetch("sensorIDsToNames");
    const sensors = await response.json();
    return sensors;
}

async function initialFetchSensorsData(temperatureChart, humidityChart) {
    gSensorIDsToNames = await fetchSensorsIDToNameMapping();
    console.log(gSensorIDsToNames);

    for (const [identifier, name] of Object.entries(gSensorIDsToNames)) {
        const dataResponse = await fetch('sensorData?' + new URLSearchParams({
            "identifier": identifier
        }))

        const sensorData = await dataResponse.json();
        console.log(sensorData);

        setSensorData(sensorData, name);

        drawData(gData, temperatureChart, 0, 1);
        drawData(gData, humidityChart, 0, 2);
    }

}

function cleanupData(data) {
    for (const [identifier, payload] of Object.entries(data)) {
        let values = payload.values;

        values.sort((a, b) => a[0] - b[0]);
        let elemsToRemove = values.length - MAX_ARR_ELEMS;
        if (elemsToRemove > 0) {
            data[identifier].values = values.slice(elemsToRemove);
        }

        const secInDay = 24 * 60 * 60;
        const currentEpoch = Math.round(Date.now() / 1000);
        const dayBeforeEpoch = currentEpoch - secInDay;

        data[identifier].values = data[identifier].values.filter((val) => val[0] > dayBeforeEpoch);
    }
}

function drawData(data, chart, xValIndex, yValIndex) {
    chart.draw(data, yValIndex);
}

function setSensorData(data, name) {
    const identifier = data["identifier"]
    const values = data["values"];

    gData[identifier] = {
        "name": name,
        "values": values
    };
}

function addSensorData(data, name) {
    const identifier = data["identifier"]
    const values = data["values"][0];

    if (gData.hasOwnProperty(identifier)) {
        gData[identifier]["values"] = gData[identifier]["values"] || [];
        gData[identifier]["values"].push(values);
    }
    else {
        setSensorData(data, name);
    }
}

window.addEventListener('load', function () {
    const temperatureCanvas = document.getElementById("temperatureCanvas");
    const temperatureChart = new MicroChart(temperatureCanvas, "Temperature");

    const humidityCanvas = document.getElementById("humidityCanvas");
    const humidityChart = new MicroChart(humidityCanvas, "Humidity");

    initialFetchSensorsData(temperatureChart, humidityChart);
    temperatureChart.draw(gData, TEMPERATURE_IDX);
    humidityChart.draw(gData, HUMIDITY_IDX);

    window.addEventListener('resize', function () {
        temperatureChart.draw(gData, TEMPERATURE_IDX);
        humidityChart.draw(gData, HUMIDITY_IDX);
    })

    if (!!window.EventSource) {
        var source = new EventSource('/events');

        source.addEventListener('open', function (e) {
            console.log("Connected");
        }, false);

        source.addEventListener('error', function (e) {
            if (e.target.readyState != EventSource.OPEN) {
                console.log("Disconnected");
            }
        }, false);

        source.addEventListener('newReading', function (e) {
            const reading = JSON.parse(e.data);
            console.log(e.data);

            let sensorName = "No name";
            if (gSensorIDsToNames.hasOwnProperty(reading.identifier)) {
                sensorName = gSensorIDsToNames[reading.identifier];
            }

            addSensorData(reading, sensorName);

            cleanupData(gData);
            temperatureChart.draw(gData, TEMPERATURE_IDX);
            humidityChart.draw(gData, HUMIDITY_IDX);
        }, false);
    }
})
