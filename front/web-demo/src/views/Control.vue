<template>
  <v-container fluid>
    <v-toolbar dense floating class=mb-0>
      <v-tooltip bottom>
        <template v-slot:activator="{ on, attrs }">
          <v-btn color="purple" icon v-bind="attrs" v-on="on" @click="change_controller">
            <v-icon v-if="controller">dangerous</v-icon>
            <v-icon v-else>tune</v-icon>
          </v-btn>  
        </template>
        <span v-if="controller">Bang Bang Controller</span>
        <span v-else>PID Controller</span>
      </v-tooltip>

      <v-btn color="primary" icon @click="download_plot">
        <v-icon>download</v-icon>
      </v-btn>     
      <v-btn color="green" icon @click="clima_resume">
        <v-icon>play_arrow</v-icon>
      </v-btn>
      <v-btn color="warning" icon @click="clima_pause">
        <v-icon>pause</v-icon>
      </v-btn>
      <v-slider v-model="target" :max="50" label="Ts:" class="align-center mt-3 ml-2 parameters">
        <template v-slot:append>
          <v-text-field v-model="target" class="mt-0 pt-0" type="text" style="width: 30px" @keypress.enter="update_parameters"/>
        </template>
      </v-slider>
      <v-slider v-model="dT" :max="2000" label="dT:" class="align-center mt-3 ml-2 parameters">
        <template v-slot:append>
          <v-text-field v-model="dT" class="mt-0 pt-0" type="text" style="width: 30px" @keypress.enter="update_parameters"/>
        </template>
      </v-slider>
      <v-slider
        v-model="Kp"
        :max="300"
        label="Kp:"
        class="align-center mt-3 ml-2 parameters"
        @mouseleave.native="hide_slider"
      >
        <template v-slot:append>
          <v-text-field
            v-model="Kp"
            class="mt-0 pt-0"
            type="text"
            style="width: 30px"
            @click="show_slider"
            @keypress.enter="update_parameters"
          ></v-text-field>
        </template>
      </v-slider>
      <v-slider
        v-model="Ki"
        :max="300"
        label="Ki:"
        class="align-center mt-3 ml-2 parameters"
        @mouseleave.native="hide_slider"
      >
        <template v-slot:append>
          <v-text-field
            v-model="Ki"
            class="mt-0 pt-0"
            type="text"
            style="width: 30px"
            @click="show_slider"
            @keypress.enter="update_parameters"
          ></v-text-field>
        </template>
      </v-slider>
      <v-slider
        v-model="Kd"
        :max="300"
        label="Kd:"
        class="align-center mt-3 ml-2 parameters"
        @mouseleave.native="hide_slider"
      >
        <template v-slot:append>
          <v-text-field
            v-model="Kd"
            class="mt-0 pt-0"
            type="text"
            style="width: 30px"
            @click="show_slider"
            @keypress.enter="update_parameters"
          ></v-text-field>
        </template>
      </v-slider>
    </v-toolbar>
    <v-card-text class=pt-0>
      <div id="plot" ref='plot'></div>
    </v-card-text>
  </v-container>
</template>
<style>
.v-speed-dial__list{
  top: -48px !important;
  left: -21px !important;
}
.parameters .v-input__append-outer{
  margin-left: 0px;
  margin-top: -3px;
}
.parameters .v-input__append-outer .v-input .v-input__control .v-input__slot .v-text-field__slot input{
  padding: 10px 0 1px;
}
.parameters .v-input__control .v-input__slot .v-slider{
  width:0px;    
  margin-right: 0px;
  opacity: 0;
}
.parameters .v-input__control .v-input__slot .v-label{
  height: 20px;
  line-height: 20px;
  margin-right: 6px;
}
#slider_animation_in{
  opacity: 1;
  animation-name: slider_animation_in;
  animation-duration: 0.5s;
  animation-fill-mode: forwards;

}
#slider_animation_out{
  animation-name: slider_animation_out;
  animation-duration: 0.5s;
  animation-fill-mode: forwards;
}

@keyframes slider_animation_in {
  0%   {width:0px;    margin-right: 0px;}
  100% {width:180px;  margin-right: 10px;}
}
@keyframes slider_animation_out {
  0%   {width:180px; opacity:1; margin-right: 10px;}
  95%  {width:5px;   opacity:1; margin-right: 2px;}
  100% {width:0px;   opacity:0; margin-right: 0px;}
}
</style>
<script>
export default {
  data () {
    return {
      controller:false,//pri PID
      timer: null,
      timer_live_plot: null,
      update_data_delta:700,
      Kp:10, Ki:0, Kd:1, dT:1,target:1,
      slider_gate:false,
      start_time:0, start_time_gate:false,
      scale: 0,
      weight: '',
      pending_trace:[],
      temp_trace:[],
      trace:[ 
      {x: [], y: [], xaxis: 'x1', yaxis: 'y1', name: 'P', mode: 'lines'},
      {x: [], y: [], xaxis: 'x1', yaxis: 'y1', name: 'I', mode: 'lines'},
      {x: [], y: [], xaxis: 'x1', yaxis: 'y1', name: 'D', mode: 'lines'},
      {x: [], y: [], xaxis: 'x1', yaxis: 'y1', name: 'Output', mode: 'lines'},
      {x: [], y: [], xaxis: 'x2', yaxis: 'y2', name: 'Measurment', mode: 'lines'},
      {x: [], y: [], xaxis: 'x2', yaxis: 'y2', name: 'Цeл', mode: 'lines',line: {dash: 'dash', width: 2}}
      ]
    }
  },
  computed: {
    get_chart_value () {
      return this.$store.state.chart_value
    }
  },
  methods: {
    updateData: function () {
      this.$ajax
        .get('/api/v1/clima/data')
        .then(data => {
          var states = data.data.states.trim().split(' ')
          if (this.start_time_gate){
            this.start_time = states[0]
            this.start_time_gate = false
          }
          for (let i = 0; i < states.length; i+= this.trace.length) {
            var time = (parseFloat(states[i]) - this.start_time)/1000
            if(this.controller == true){
              this.pending_trace.push({
                x: [[time], [time], [time]],
                y: [[parseFloat(states[i+1])], [parseFloat(states[i+2])], [this.target]]
              })
            }else{
              this.pending_trace.push({
                x: [[time], [time], [time], [time], [time], [time]],
                y: [[parseFloat(states[i+1])], [parseFloat(states[i+2])], 
                    [parseFloat(states[i+3])], [parseFloat(states[i+4])], 
                    [parseFloat(states[i+5])], [this.target]]
              })  
            }
          }
        })
        .catch(error => {
          console.log(error)
        })
    },
    live_plot: function(){
      if (this.pending_trace.length != 0){
        Plotly.extendTraces(this.$refs.plot, this.pending_trace.shift(), this.controller ? [0,1,2]:[0,1,2,3,4,5])
        var last_time = this.trace[0].x[this.trace[0].x.length -1]
        if(last_time > 10) {
          Plotly.relayout(this.$refs.plot,{
              xaxis2: {
                range: [last_time-10,last_time]
              },
              xaxis: {
                range: [last_time-10,last_time]
              }
          });
        }
      }
    },
    update_parameters: function(){
      this.$ajax
        .post('/api/v1/clima/update_parameters', {
          Kp: parseFloat(this.Kp),
          Ki: parseFloat(this.Ki),
          Kd: parseFloat(this.Kd),
          dT: parseFloat(this.dT),
          target: parseFloat(this.target)
        })
        .then(data => {
          console.log(data.data)
        })
        .catch(error => {
          console.log(error)
        })
    },
    clima_resume: function () {
      this.$ajax
        .get('/api/v1/clima/resume')
        .then(data => {
          for (let i = 0; i < this.trace.length; i++){
            this.trace[i].x = []
            this.trace[i].y = []
          }
          for (let i = 0; i < this.temp_trace.length && this.controller == true; i++){
            this.temp_trace[i].x = []
            this.temp_trace[i].y = []
          }
          this.pending_trace=[]
          Plotly.relayout(this.$refs.plot,{
            xaxis2: {
              range: [0,10]
            },
            xaxis: {
              range: [0,10]
            }
          });
          this.timer = setInterval(this.updateData, this.update_data_delta)
          this.timer_live_plot = setInterval(this.live_plot, parseInt(this.dT))
          this.start_time_gate = true
          console.log(data.data)
        })
        .catch(error => {
          console.log(error)
        })
    },
    clima_pause: function () {
      this.$ajax
        .get('/api/v1/clima/pause')
        .then(data => {
          this.start_time_gate = false
          clearInterval(this.timer)
          clearInterval(this.timer_live_plot)
          console.log(data.data)
        })
        .catch(error => {
          console.log(error)
        })
    },
    change_controller: function (){
      this.controller = !this.controller
      this.$ajax
      .get('/api/v1/clima/change_controller')
      .then(data => {
        if(this.controller){
          this.temp_trace = this.trace.splice(0,3)
        }else{
          this.trace.splice(0,0,this.temp_trace[0],this.temp_trace[1],this.temp_trace[2])
        }
        Plotly.redraw(this.$refs.plot)
        console.log(data.data)
      })
      .catch(error => {
        console.log(error)
      })
    },
    download_plot: function () {
      var csv_data = (this.controller)?['Time,Output,Measurement,Target']:['Time,P,I,D,Output,Measurement,Target'];
      for (var i = 0; i < this.trace[0].y.length; i++) {
          var csvrow = [this.trace[0].x[i]];
          for (var j = 0; j <this.trace.length ; j++) {
            csvrow.push(this.trace[j].y[i]);
          }
          csv_data.push(csvrow.join(","));
      }
      csv_data = csv_data.join('\n');
      var CSVFile = new Blob([csv_data], {
          type: "text/csv"
      });
      var temp_link = document.createElement('a');
      temp_link.download = "Log.csv";
      var url = window.URL.createObjectURL(CSVFile);
      temp_link.href = url;
      temp_link.style.display = "none";
      document.body.appendChild(temp_link);
      temp_link.click();
      document.body.removeChild(temp_link);
    },
    show_slider: function(el){
      var root = el.srcElement.parentElement.parentElement.parentElement.parentElement.parentElement.parentElement;
      var slider = root.children[0].children[0].children[1];
      slider.setAttribute('id','slider_animation_in');
      this.slider_gate = true
    },
    hide_slider: function(el){
      if ((el.srcElement === el.toElement) || !this.slider_gate) return;
      var slider = el.srcElement.children[0].children[0].children[1];
      slider.setAttribute('id','slider_animation_out');
      this.update_parameters()
      this.slider_gate = false
    },
  },
  mounted () {
    clearInterval(this.timer)
    this.$ajax
      .get('/api/v1/clima/init')
      .then(data => {
        this.Kp = data.data.Kp;
        this.Ki = data.data.Ki;
        this.Kd = data.data.Kd;
        this.dT = data.data.dT;
        this.target = data.data.target;
      })
      .catch(error => {
        console.log(error)
      })

    var layout = {
     
      height:550,
      plot_bgcolor:'#212121',
      paper_bgcolor:'#212121',
      grid: {rows: 2, columns: 1, pattern: 'independent'},
      xaxis2: { range: [0,10] },
      xaxis:  { range: [0,10] },
      font: {
        color: 'white'
      }
    };
    var config = {responsive: true}
    Plotly.newPlot(this.$refs.plot, this.trace, layout,config)
  },
  destroyed: function () {
    clearInterval(this.timer)
  }
}
</script>
